/* Start a fresh I2S -> UAC2 bridge only while the USB host records audio. */
#define _POSIX_C_SOURCE 200809L
#include <alloca.h>
#include <alsa/asoundlib.h>
#include <errno.h>
#include <glob.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "audio-control.h"

#ifndef ALSALOOP_PATH
#define ALSALOOP_PATH "/usr/bin/alsaloop"
#endif
#ifndef PROC_ROOT
#define PROC_ROOT "/proc"
#endif

static volatile sig_atomic_t stopping;

static void stop_signal(int signal_number)
{
	(void)signal_number;
	stopping = 1;
}

static double monotonic_seconds(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec + now.tv_nsec / 1000000000.0;
}

/* Read small proc files while the bridge is alive. The old 60-second system
 * logger routinely missed entire microphone sessions. No PCM is opened here
 * and no signal interrupts alsaloop's audio processing. */
static void diagnostic_file(const char *path)
{
	char data[1024];
	FILE *file = fopen(path, "r");
	if (!file)
		return;
	fprintf(stderr, "%s\n", path);
	for (unsigned int i = 0; i < 4; ++i) {
		size_t count = fread(data, 1, sizeof(data), file);
		if (!count)
			break;
		fwrite(data, 1, count, stderr);
	}
	fclose(file);
}

static void diagnostic_snapshot(pid_t child, const char *trigger)
{
	if(!audio_diagnostics_get())return;
	static unsigned int snapshots;
	glob_t paths = {0};
	char path[128];
	/* One-off diagnostics must not grow a RAM-backed log indefinitely. */
	if (snapshots >= 512)
		return;
	++snapshots;
	fprintf(stderr, "microphone diagnostic uptime=%.6f pid=%ld trigger=%s\n",
		monotonic_seconds(), (long)child, trigger);
	if (!glob(PROC_ROOT "/asound/card*/pcm*/sub*/status", 0, NULL, &paths)) {
		for (size_t i = 0; i < paths.gl_pathc; ++i)
			diagnostic_file(paths.gl_pathv[i]);
	}
	globfree(&paths);
	memset(&paths, 0, sizeof(paths));
	if (!glob(PROC_ROOT "/asound/card*/pcm*/sub*/hw_params", 0, NULL, &paths)) {
		for (size_t i = 0; i < paths.gl_pathc; ++i)
			diagnostic_file(paths.gl_pathv[i]);
	}
	globfree(&paths);
	snprintf(path, sizeof(path), PROC_ROOT "/%ld/stat", (long)child);
	diagnostic_file(path);
	diagnostic_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
	fflush(stderr);
}

static void stop_bridge(pid_t *child)
{
	if (*child <= 0)
		return;
	diagnostic_snapshot(*child, "pre-stop");
	kill(*child, SIGTERM);
	/* alsaloop normally exits on TERM. Bound shutdown even if it is stuck. */
	for (int i = 0; i < 20; ++i) {
		pid_t result = waitpid(*child, NULL, WNOHANG);
		if (result == *child || (result < 0 && errno == ECHILD))
			goto done;
		struct timespec delay = {0, 50000000};
		nanosleep(&delay, NULL);
	}
	kill(*child, SIGKILL);
	while (waitpid(*child, NULL, 0) < 0 && errno == EINTR) {}
done:
	fprintf(stderr, "microphone bridge stopped\n");
	*child = 0;
}

static pid_t start_bridge(snd_ctl_t *control)
{
	pid_t child = fork();
	if (child == 0) {
		signal(SIGTERM, SIG_DFL);
		signal(SIGINT, SIG_DFL);
		snd_ctl_close(control);
		execl(ALSALOOP_PATH, "alsaloop", "-C", "pisight_mic",
		      "-P", "hw:CARD=UAC2Gadget,DEV=0", "-f", "S16_LE",
		      "-c", "1", "-r", "48000", "-t", "50000", "-n",
		      (char *)NULL);
		perror("exec alsaloop");
		_exit(127);
	}
	if (child < 0)
		perror("fork alsaloop");
	else
		fprintf(stderr, "microphone bridge started, pid %ld\n", (long)child);
	return child > 0 ? child : 0;
}

int main(void)
{
	snd_ctl_t *control;
	snd_ctl_elem_value_t *value;
	snd_ctl_event_t *event;
	struct sigaction action = {0};
	struct pollfd *fds = NULL;
	pid_t child = 0;
	double retry_at = 0, diagnostic_at = 0;
	long previous_rate = -1;
	int result = 1, error, count;

	action.sa_handler = stop_signal;
	sigemptyset(&action.sa_mask);
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGINT, &action, NULL);

	error = snd_ctl_open(&control, "hw:CARD=UAC2Gadget", SND_CTL_NONBLOCK);
	if (error < 0) {
		fprintf(stderr, "open UAC2 control: %s\n", snd_strerror(error));
		return 1;
	}
	/* Subscribe first, then read: a host opening during startup is not lost. */
	error = snd_ctl_subscribe_events(control, 1);
	if (error < 0)
		goto alsa_error;
	count = snd_ctl_poll_descriptors_count(control);
	if (count <= 0)
		goto done;
	fds = calloc((size_t)count, sizeof(*fds));
	if (!fds)
		goto done;
	error = snd_ctl_poll_descriptors(control, fds, (unsigned)count);
	if (error < 0)
		goto alsa_error;
	snd_ctl_elem_value_alloca(&value);
	snd_ctl_event_alloca(&event);
	snd_ctl_elem_value_set_interface(value, SND_CTL_ELEM_IFACE_PCM);
	snd_ctl_elem_value_set_name(value, "Playback Rate");
	/* Establish the initial state after discarding notifications queued
	 * between subscription and this first read. There is no child yet. */
	while ((error = snd_ctl_read(control, event)) > 0) {}
	if (error < 0 && error != -EAGAIN && error != -EINTR)
		goto alsa_error;

	while (!stopping) {
		if (child > 0 && waitpid(child, NULL, WNOHANG) == child) {
			fprintf(stderr, "microphone bridge exited; retry after one second\n");
			child = 0;
			retry_at = monotonic_seconds() + 1;
		}
		error = snd_ctl_elem_read(control, value);
		if (error < 0)
			goto alsa_error;
		long rate = snd_ctl_elem_value_get_integer(value, 0);
		if (rate != previous_rate) {
			fprintf(stderr, "USB microphone playback rate: %ld\n", rate);
			previous_rate = rate;
		}
		if (rate != 48000) {
			stop_bridge(&child);
		} else if (!child && monotonic_seconds() >= retry_at && !stopping) {
			child = start_bridge(control);
			if (!child)
				retry_at = monotonic_seconds() + 1;
			else
				diagnostic_at = monotonic_seconds() + 1;
		}
		if (child && monotonic_seconds() >= diagnostic_at) {
			diagnostic_snapshot(child, "periodic");
			diagnostic_at = monotonic_seconds() + 5;
		}

		/* Control events wake immediately; timeout also reaps crashed children. */
		int ready = poll(fds, (nfds_t)count, 250);
		if (ready < 0) {
			if (errno == EINTR)
				continue;
			perror("poll UAC2 control");
			goto done;
		}
		if (ready > 0) {
			int rate_event = 0;
			for (int i = 0; i < count; ++i)
				if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
					goto done;
			/* A close/open can coalesce to the same final rate. The rate
			 * event carries no history, so refresh a live bridge on any
			 * relevant value notification. Ignore other controls. */
			while ((error = snd_ctl_read(control, event)) > 0) {
				if (snd_ctl_event_get_type(event) == SND_CTL_EVENT_ELEM &&
				    (snd_ctl_event_elem_get_mask(event) & SND_CTL_EVENT_MASK_VALUE) &&
				    snd_ctl_event_elem_get_interface(event) == SND_CTL_ELEM_IFACE_PCM &&
				    !strcmp(snd_ctl_event_elem_get_name(event), "Playback Rate"))
					rate_event = 1;
			}
			if (error < 0 && error != -EAGAIN && error != -EINTR)
				goto alsa_error;
			if (rate_event)
				stop_bridge(&child);
		}
	}
	result = 0;
	goto done;
alsa_error:
	fprintf(stderr, "UAC2 control: %s\n", snd_strerror(error));
done:
	stop_bridge(&child);
	free(fds);
	snd_ctl_close(control);
	return result;
}
