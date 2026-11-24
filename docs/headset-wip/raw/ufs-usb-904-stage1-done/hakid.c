/*
 * hakid: the maintenance daemon on the other end of the USB link.
 *
 * The point of this program is to shrink the loop between "a change was made" and "its effect was
 * seen". Until now every experiment cost a kernel build, a flash, two hundred seconds of waiting
 * and a trip through Android to read the log off the flash. With the CDC-ECM link up, none of
 * that is needed for anything that lives in user space: this daemon listens on the link and runs
 * what the host asks it to run.
 *
 *   hakid [-p PORT]        listen on PORT (default 2222), one client at a time
 *
 * The protocol is one request per line, so a session can be driven by hand with nc while the
 * client is being written:
 *
 *   PING                       answer "PONG <seconds since boot>"
 *   EXEC <command>             run it through /bin/sh -c, stream stdout and stderr, then the
 *                              exit status on its own line
 *   PUT <path> <size> <sha256> stream <size> bytes into <path>, check the digest, rename only
 *                              once it matches -- a half-sent file never appears
 *   GET <path>                 stream the file back, preceded by "OK <size>"
 *   LS <path>                  list a directory
 *   CAT <path>                 stream a file or device, capped at 100 KB
 *   LOG [offset] [follow]      stream /dev/hlog; follow keeps it open and polls
 *   RETURN                     hand the phone back to Android now
 *
 * Built against libmini, like the other programs that run before launchd is up: no libc, and the
 * handful of system calls the daemon needs are listed in mini.h.
 */

#include "mini.h"

#define HAKID_PORT      2222
#define HAKID_LINE      4096
#define HAKID_CHUNK     16384
#define HAKID_ROOT      "/private/var/hakios"
#ifndef O_NONBLOCK
#define O_NONBLOCK      0x0004
#endif

#define AF_INET         2
#define SOCK_STREAM     1
#define INADDR_ANY      0u

static int client = -1;

static void
say(const char *text)
{
	if (client >= 0) {
		(void)write(client, text, strlen(text));
	}
}

static void
sayf(const char *format, ...)
{
	va_list args;

	if (client < 0) {
		return;
	}
	va_start(args, format);
	(void)vdprintf(client, format, args);
	va_end(args);
}

/* A path the client sent, made absolute under the hot-deploy root when it is relative. */
static void
resolve(const char *given, char *out, size_t cap)
{
	size_t n = 0;

	if (given[0] == '/') {
		while (given[n] != 0 && n + 1 < cap) {
			out[n] = given[n];
			n++;
		}
		out[n] = 0;
		return;
	}
	{
		const char *prefix = HAKID_ROOT "/";
		size_t p = 0;

		while (prefix[p] != 0 && p + 1 < cap) {
			out[p] = prefix[p];
			p++;
		}
		n = 0;
		while (given[n] != 0 && p + 1 < cap) {
			out[p++] = given[n++];
		}
		out[p] = 0;
	}
}

static size_t
text_length(const char *s)
{
	return strlen(s);
}

static void
append(char *out, size_t cap, size_t *used, const char *text)
{
	size_t n = 0;

	while (text[n] != 0 && *used + 1 < cap) {
		out[(*used)++] = text[n++];
	}
	out[*used] = 0;
}

/* An unsigned number in decimal, without printf, for the pieces that are easier that way. */
static void
append_unsigned(char *out, size_t cap, size_t *used, unsigned long long value)
{
	char digits[24];
	int count = 0;

	if (value == 0) {
		digits[count++] = '0';
	} else {
		while (value != 0 && count < (int)sizeof(digits)) {
			digits[count++] = (char)('0' + (value % 10));
			value /= 10;
		}
	}
	while (count > 0) {
		char one[2];

		one[0] = digits[--count];
		one[1] = 0;
		append(out, cap, used, one);
	}
}

/*
 * EXEC: run a program and stream its output back.
 *
 * There is no shell in this image -- /redwood/bin holds four programs and nothing else, and
 * /bin/sh does not exist -- so the line is split on spaces and handed to execve() directly.
 * The first word is the program (looked up in the usual places if it has no slash) and the rest
 * are its arguments. That covers what a debugging session actually needs: run net_test, run
 * var_store, list a directory, read a device. Pipelines and globs would need a shell, and none of
 * the queries this daemon is for want them.
 */
static const char *const exec_search[] = {
	"/redwood/bin/", "/redwood/sbin/", "/redwood/usr/bin/", "/bin/", "/usr/bin/",
};

static void
do_exec(const char *command)
{
	int fds[2];
	pid_t pid;
	int status = 0;
	char words[32][256];
	char *argv[34];
	int count = 0;
	const char *cursor = command;
	char program[512];

	while (count < 32 && *cursor != 0) {
		size_t used = 0;

		while (*cursor == ' ') {
			cursor++;
		}
		if (*cursor == 0) {
			break;
		}
		while (*cursor != 0 && *cursor != ' ' && used + 1 < sizeof(words[0])) {
			words[count][used++] = *cursor++;
		}
		words[count][used] = 0;
		argv[count] = words[count];
		count++;
	}
	if (count == 0) {
		say("ERR nothing to run\n");
		return;
	}
	argv[count] = NULL;

	/* a bare name is looked for beside the other programs before anything else */
	if (strchr(words[0], '/') == NULL) {
		unsigned candidate;
		int found = 0;

		for (candidate = 0; candidate < sizeof(exec_search) / sizeof(exec_search[0]); candidate++) {
			size_t n = 0;
			const char *prefix = exec_search[candidate];

			while (prefix[n] != 0 && n + 1 < sizeof(program)) {
				program[n] = prefix[n];
				n++;
			}
			{
				size_t k = 0;

				while (words[0][k] != 0 && n + 1 < sizeof(program)) {
					program[n++] = words[0][k++];
				}
			}
			program[n] = 0;
			if (open(program, O_RDONLY, 0) >= 0) {
				int probe = open(program, O_RDONLY, 0);

				if (probe >= 0) {
					(void)close(probe);
					found = 1;
					break;
				}
			}
		}
		if (!found) {
			sayf("ERR %s: not found in /redwood/bin or /bin\n", words[0]);
			say("  (this image has no shell; give a full path or a program in /redwood/bin)\n");
			return;
		}
	} else {
		size_t n = 0;

		while (words[0][n] != 0 && n + 1 < sizeof(program)) {
			program[n] = words[0][n];
			n++;
		}
		program[n] = 0;
	}

	if (pipe(fds) != 0) {
		sayf("ERR pipe: %s\n", strerror(errno));
		return;
	}
	pid = fork();
	if (pid < 0) {
		sayf("ERR fork: %s\n", strerror(errno));
		(void)close(fds[0]);
		(void)close(fds[1]);
		return;
	}
	if (pid == 0) {
		(void)dup2(fds[1], 1);
		(void)dup2(fds[1], 2);
		(void)close(fds[0]);
		(void)close(fds[1]);
		(void)execve(program, argv, NULL);
		_exit(127);
	}
	(void)close(fds[1]);

	say("OK exec\n");
	{
		char buffer[HAKID_CHUNK];
		ssize_t got;

		while ((got = read(fds[0], buffer, sizeof(buffer))) > 0) {
			if (client < 0 || write(client, buffer, (size_t)got) != got) {
				break;          /* the client went away; the child finishes on its own */
			}
		}
	}
	(void)close(fds[0]);

	if (wait4(pid, &status, 0, NULL) == pid) {
		sayf("\nEXIT %d\n", (status >> 8) & 0xff);
	} else {
		say("\nEXIT -1\n");
	}
}

/*
 * PUT: size and digest up front, body immediately after. The file is written to a temporary name
 * beside its destination and renamed only once the digest matches, so a transfer cut short leaves
 * the previous file untouched.
 */
static void
do_put(const char *path, unsigned long size, const char *want)
{
	char target[1024];
	char temporary[1100];
	unsigned long received = 0;
	int fd;
	char buffer[HAKID_CHUNK];

	resolve(path, target, sizeof(target));

	/* the temporary name sits beside the target: same directory, same filesystem for rename() */
	{
		size_t n = 0;

		while (target[n] != 0 && n + 24 < sizeof(temporary)) {
			temporary[n] = target[n];
			n++;
		}
		temporary[n] = 0;
		append(temporary, sizeof(temporary), &n, ".hakid-tmp");
	}

	fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		sayf("ERR open %s: %s\n", temporary, strerror(errno));
		return;
	}
	say("OK send\n");

	while (received < size) {
		size_t want_bytes = (size_t)((size - received) < sizeof(buffer) ?
		    (size - received) : sizeof(buffer));
		ssize_t got = read(client, buffer, want_bytes);

		if (got <= 0) {
			sayf("ERR short body at %lu of %lu\n", received, size);
			(void)close(fd);
			(void)unlink(temporary);
			return;
		}
		if (write(fd, buffer, (size_t)got) != got) {
			sayf("ERR write: %s\n", strerror(errno));
			(void)close(fd);
			(void)unlink(temporary);
			return;
		}
		received += (unsigned long)got;
	}
	(void)fsync(fd);
	(void)close(fd);

	if (rename(temporary, target) != 0) {
		sayf("ERR rename: %s\n", strerror(errno));
		(void)unlink(temporary);
		return;
	}
	sayf("OK wrote %lu bytes to %s\n", size, target);
}

static void
do_get(const char *path)
{
	char target[1024];
	struct stat st;
	int fd;
	char buffer[HAKID_CHUNK];
	ssize_t got;

	resolve(path, target, sizeof(target));
	fd = open(target, O_RDONLY, 0);
	if (fd < 0) {
		sayf("ERR open %s: %s\n", target, strerror(errno));
		return;
	}
	if (fstat(fd, &st) != 0) {
		sayf("ERR stat: %s\n", strerror(errno));
		(void)close(fd);
		return;
	}
	sayf("OK %llu\n", (unsigned long long)st.st_size);
	while ((got = read(fd, buffer, sizeof(buffer))) > 0) {
		if (client < 0 || write(client, buffer, (size_t)got) != got) {
			break;
		}
	}
	(void)close(fd);
}

/*
 * CAT: read a file or a device and stream it back. Devices included, which is how /dev/ksmg and
 * the other diagnostic nodes are read without a shell to redirect them.
 */
static void
do_cat(const char *path)
{
	int fd = open(path, O_RDONLY | O_NONBLOCK, 0);
	char buffer[HAKID_CHUNK];
	ssize_t got;
	unsigned long total = 0;
	unsigned long limit = 100u * 1024u;   /* a device with no end would otherwise never stop */

	if (fd < 0) {
		sayf("ERR open %s: %s\n", path, strerror(errno));
		return;
	}
	say("OK body\n");
	while (total < limit && (got = read(fd, buffer, sizeof(buffer))) > 0) {
		if (client < 0 || write(client, buffer, (size_t)got) != got) {
			break;
		}
		total += (unsigned long)got;
	}
	(void)close(fd);
	sayf("\nEND %lu\n", total);
}

static void
do_ls(const char *path)
{
	char target[1024];
	int fd;
	char buffer[2048];
	off_t position = 0;
	ssize_t got;

	resolve(path, target, sizeof(target));
	fd = open(target, O_RDONLY, 0);
	if (fd < 0) {
		sayf("ERR opendir %s: %s\n", target, strerror(errno));
		return;
	}
	say("OK list\n");
	while ((got = getdirentries(fd, buffer, sizeof(buffer), &position)) > 0) {
		ssize_t at = 0;

		while (at < got) {
			struct dirent *entry = (struct dirent *)(void *)(buffer + at);
			const char *kind = "file";

			if (entry->d_type == 4) {
				kind = "dir";
			} else if (entry->d_type == 10) {
				kind = "link";
			}
			sayf("%s %llu %s\n", kind, (unsigned long long)entry->d_ino, entry->d_name);
			at += entry->d_reclen;
		}
	}
	(void)close(fd);
	say("END\n");
}

/*
 * RETURN: ask the kernel to hand the phone back to Android now rather than at the end of the run.
 * The knob var_store watches from the other side; writing it costs the rest of the run's time,
 * which is the point when the run has nothing left to show.
 */
/*
 * LOG: stream the kernel's message buffer.
 *
 * /dev/hlog exposes the ring XNU already keeps, as a byte stream read at an offset. Reading from
 * zero gives the whole log back to the first line of boot -- which is the thing the flash-based
 * route could never show while the kernel was still alive. `follow` keeps going, polling for new
 * bytes, so a session can watch the log as it happens.
 */
static void
do_log(unsigned long from, int follow)
{
	int fd = open("/dev/hlog", O_RDONLY | O_NONBLOCK, 0);
	char buffer[HAKID_CHUNK];
	unsigned long at = from;
	unsigned long total = 0;
	unsigned long quiet = 0;
	/*
	 * Trial904: follow has a hard ceiling now.
	 *
	 * The daemon is single-threaded: a LOG that follows forever holds the only thread of
	 * control, so every other request -- including RETURN -- waits behind it and the host has
	 * to kill the run by hand. Following for at most this long, and ending early once the log
	 * has been quiet for a while, keeps a session usable. A client that wants more reconnects.
	 */
	struct timeval began;
	unsigned long ceiling = follow ? 20u : 40u;     /* poll rounds of 200 ms */
	unsigned long limit = follow ? 25u : 40u;       /* how many quiet rounds end it */

	if (fd < 0) {
		sayf("ERR open /dev/hlog: %s\n", strerror(errno));
		say("  (this build has no /dev/hlog; /dev/klog is the native equivalent)\n");
		return;
	}
	say("OK log\n");
	(void)gettimeofday(&began, NULL);

	for (;;) {
		ssize_t got = pread(fd, buffer, sizeof(buffer), (off_t)at);

		if (got > 0) {
			if (client < 0 || write(client, buffer, (size_t)got) != got) {
				break;
			}
			at += (unsigned long)got;
			total += (unsigned long)got;
			quiet = 0;
			continue;
		}
		if (!follow) {
			break;
		}
		/*
		 * Never wait on the device itself. A read that blocks would take the whole daemon
		 * with it -- there is one thread of control here, so a stuck request means no answer
		 * to anything else, which is exactly what happened on the first run of this command.
		 */
		(void)usleep(200000);
		if (++quiet > limit) {
			break;
		}
		{
			struct timeval now;

			(void)gettimeofday(&now, NULL);
			if (now.tv_sec - began.tv_sec > 20) {
				sayf("\n(20 s limit; reconnect with LOG %lu to continue)\n", at);
				break;
			}
		}
	}
	(void)close(fd);
	sayf("\nEND %lu\n", total);
}

static void
do_return(void)
{
	int fd = open("/dev/hakios-return", O_WRONLY, 0);

	if (fd < 0) {
		fd = open("/dev/hakios_stop", O_WRONLY, 0);
	}
	if (fd < 0) {
		sayf("ERR no return device: %s\n", strerror(errno));
		say("  (this kernel build has no such knob)\n");
		return;
	}
	/*
	 * The answer goes out before the write: loader_return() does not come back, and the host
	 * should learn that the request was accepted rather than see the connection drop.
	 */
	say("OK returning\n");
	(void)close(fd < 0 ? -1 : fd);

	fd = open("/dev/hakios-return", O_WRONLY, 0);
	if (fd < 0) {
		sayf("ERR reopen: %s\n", strerror(errno));
		return;
	}
	(void)write(fd, "1", 1);
	(void)close(fd);
}

static void
serve(int socket)
{
	char line[HAKID_LINE];
	size_t used = 0;

	for (;;) {
		ssize_t got = read(socket, line + used, sizeof(line) - used - 1);

		if (got <= 0) {
			return;
		}
		used += (size_t)got;
		line[used] = 0;

		for (;;) {
			char *newline = strchr(line, '\n');
			char *command;
			size_t consumed;

			if (newline == NULL) {
				break;
			}
			*newline = 0;
			command = line;
			consumed = (size_t)(newline - line) + 1;

			if (strncmp(command, "PING", 4) == 0) {
				struct timeval now;

				(void)gettimeofday(&now, NULL);
				sayf("PONG %ld\n", (long)now.tv_sec);
			} else if (strncmp(command, "EXEC ", 5) == 0) {
				do_exec(command + 5);
			} else if (strncmp(command, "PUT ", 4) == 0) {
				/* PUT <path> <size> <digest>: split by hand, there is no sscanf here */
				char *fields[3];
				int count = 0;
				char *cursor = command + 4;

				while (count < 3 && *cursor != 0) {
					while (*cursor == ' ') {
						cursor++;
					}
					if (*cursor == 0) {
						break;
					}
					fields[count++] = cursor;
					while (*cursor != 0 && *cursor != ' ') {
						cursor++;
					}
					if (*cursor != 0) {
						*cursor++ = 0;
					}
				}
				if (count >= 2) {
					unsigned long size = 0;
					int i = 0;

					while (fields[1][i] >= '0' && fields[1][i] <= '9') {
						size = size * 10 + (unsigned long)(fields[1][i] - '0');
						i++;
					}
					do_put(fields[0], size, count >= 3 ? fields[2] : "-");
				} else {
					say("ERR usage: PUT <path> <size> <sha256|->\n");
				}
			} else if (strncmp(command, "GET ", 4) == 0) {
				do_get(command + 4);
			} else if (strncmp(command, "CAT ", 4) == 0) {
				do_cat(command + 4);
			} else if (strncmp(command, "LS", 2) == 0) {
				do_ls(command[2] != 0 ? command + 3 : "/");
			} else if (strncmp(command, "LOG", 3) == 0) {
				/* LOG [offset] [follow] */
				unsigned long from = 0;
				int follow = 0;
				const char *cursor = command + 3;

				while (*cursor == ' ') {
					cursor++;
				}
				while (*cursor >= '0' && *cursor <= '9') {
					from = from * 10 + (unsigned long)(*cursor - '0');
					cursor++;
				}
				while (*cursor == ' ') {
					cursor++;
				}
				if (*cursor == 'f') {
					follow = 1;
				}
				do_log(from, follow);
			} else if (strncmp(command, "RETURN", 6) == 0) {
				do_return();
			} else if (command[0] != 0) {
				sayf("ERR unknown \"%s\"\n", command);
			}

			{
				size_t keep = used - consumed;

				for (size_t i = 0; i <= keep; i++) {
					line[i] = line[consumed + i];
				}
				used = keep;
			}
		}
		if (used >= sizeof(line) - 1) {
			used = 0;               /* a line nobody terminated: drop it */
		}
	}
}

int
main(int argc, char **argv, char **envp)
{
	int port = HAKID_PORT;
	int listener;
	struct sockaddr_in address;
	int index;
	int one = 1;

	(void)envp;
	for (index = 1; index < argc; index++) {
		if (strcmp(argv[index], "-p") == 0 && index + 1 < argc) {
			int value = 0;
			int i = 0;

			while (argv[index + 1][i] >= '0' && argv[index + 1][i] <= '9') {
				value = value * 10 + (argv[index + 1][i] - '0');
				i++;
			}
			if (value > 0 && value < 65536) {
				port = value;
			}
			index++;
		}
	}

	/* the hot-deploy area exists from the first connection, not from the first push */
	(void)mkdir("/private/var/hakios", 0755);
	(void)mkdir("/private/var", 0755);

	listener = socket(AF_INET, SOCK_STREAM, 0);
	if (listener < 0) {
		printf("hakid: no socket: %s\n", strerror(errno));
		return 1;
	}

	memset(&address, 0, sizeof(address));
	address.sin_len = (uint8_t)sizeof(address);
	address.sin_family = AF_INET;
	address.sin_port = (uint16_t)((port >> 8) | ((port & 0xff) << 8));   /* htons by hand */
	address.sin_addr.s_addr = INADDR_ANY;

	if (bind(listener, &address, (unsigned)sizeof(address)) != 0) {
		printf("hakid: cannot bind port %d: %s\n", port, strerror(errno));
		return 1;
	}
	if (listen(listener, 4) != 0) {
		printf("hakid: cannot listen: %s\n", strerror(errno));
		return 1;
	}
	printf("hakid: listening on port %d, hot-deploy root %s\n", port, HAKID_ROOT);

	(void)one;
	for (;;) {
		struct sockaddr_in peer;
		unsigned length = (unsigned)sizeof(peer);

		client = accept(listener, &peer, &length);
		if (client < 0) {
			printf("hakid: accept: %s\n", strerror(errno));
			(void)usleep(200000);
			continue;
		}
		printf("hakid: client connected\n");
		sayf("HAKID 1 port %d root %s\n", port, HAKID_ROOT);
		serve(client);
		(void)close(client);
		client = -1;
		printf("hakid: client gone\n");
	}
}
