/*
 * Copyright © 2024 konsolebox
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the “Software”), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_PATH_SIZE 1024

static const char *IMPLEMENTATIONS[] = {
	"ruby18", "ruby19", "ruby20", "ruby21", "ruby22", "ruby23", "ruby24", "ruby25", "ruby26",
	"ruby27", "ruby30", "ruby31", "ruby32", "ruby33", "jruby", "rbx", NULL
};

typedef struct { bool autopick; } options_t;

static void die(const char *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	fprintf(stderr, "rubyexec: ");
	vfprintf(stderr, msg, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

static void *do_malloc(size_t size)
{
	void *p = malloc(size);

	if (p == NULL)
		die("Unable to allocate memory: %s\n", strerror(errno));

	return p;
}

static char *resolve_path(const char *path)
{
	char buf[MAX_PATH_SIZE];
	size_t size = readlink(path, buf, sizeof(buf));

	if (size == -1)
		die("Failed to resolve %s: %s\n", path, strerror(errno));

	if (size >= sizeof(buf))
		die("Resolved path of %s is too long.\n", path);

	buf[size] = '\0';
	return strdup(buf);
}

static char *strconcat(const char *str1, ...)
{
	va_list ap;
	va_start(ap, str1);
	const char *str;
	size_t total_length = strlen(str1);

	while ((str = va_arg(ap, const char *)) != NULL)
		total_length += strlen(str);

	va_end(ap);
	char *buf = do_malloc(total_length + 1);
	char *p = buf;
	strcpy(p, str1);
	p += strlen(str1);
	va_start(ap, str1);

	while ((str = va_arg(ap, const char *)) != NULL) {
		strcpy(p, str);
		p += strlen(str);
	}

	va_end(ap);
	assert(buf + total_length == p);
	assert(buf[total_length] == '\0');
	return buf;
}

static bool in(const char *null_terminated[], const char *str)
{
	const char **p;

	for (p = null_terminated; *p != NULL; ++p)
		if (strcmp(*p, str) == 0)
			return true;

	return false;
}

static const char **get_valid_implementations_and_options(char *argv1, options_t *options)
{
	const char **valid_implementations = do_malloc(sizeof(IMPLEMENTATIONS));
	const char **p = valid_implementations;
	*p = NULL;
	options->autopick = false;

	for (char *str = strtok(argv1, ","); str != NULL; str = strtok(NULL, ",")) {
		if (strcmp(str, "--autopick") == 0) {
			options->autopick = true;
		} else if (*valid_implementations == NULL || !in(valid_implementations, str)) {
			if (in(IMPLEMENTATIONS, str)) {
				*p = str;
				*++p = NULL;
			}
		}
	}

	if (*valid_implementations == NULL)
		die("No valid implementations found.\n");

	return valid_implementations;
}

static char *autopick_implementation(char *dir, const char **valid_implementations)
{
	for (const char **p = valid_implementations; *p != NULL; ++p) {
		char *path = strconcat(dir, "/", *p, NULL);

		if (access(path, F_OK) == 0)
			return path;

		free(path);
	}

	die("No usable implementations found.\n");
	return NULL;
}

static char **create_new_argv(int argc, char **argv, const char *new_argv0)
{
	char **new_argv = do_malloc(argc);
	new_argv[0] = strdup(new_argv0);

	for (int i = 2; i < argc; ++i)
		new_argv[i - 1] = argv[i];

	new_argv[argc - 1] = NULL;
	return new_argv;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "rubyexec: Invalid number of arguments.\n");
		return 2;
	}

	if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
		fprintf(stderr, "rubyexec: Usage: %s impl,... [args]\n", argv[0]);
		return 2;
	}

	options_t options;
	const char **valid_implementations = get_valid_implementations_and_options(argv[1], &options);
	char *rubyexec = resolve_path("/proc/self/exe");
	char *rubyexec_dir = dirname(rubyexec);
	char *ruby = strconcat(rubyexec_dir, "/ruby", NULL);
	char *resolved_ruby = resolve_path(ruby);
	char *selected_impl = basename(resolved_ruby);
	char *impl_path;

	if (in(valid_implementations, selected_impl)) {
		impl_path = *resolved_ruby == '/' ? resolved_ruby :
				strconcat(rubyexec_dir, "/", resolved_ruby, NULL);
	} else if (options.autopick) {
		impl_path = autopick_implementation(rubyexec_dir, valid_implementations);
	} else {
		die("Script does not support currently selected Ruby implementation.\n");
	}

	execv(impl_path, create_new_argv(argc, argv, impl_path));
	die("%s failed to execute: %s\n", impl_path, strerror(errno));
	return 1;
}
