/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <limits.h>
#include <sign/io.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

bool file_read(const char* path, sign_buffer* bytes) {
	if (!path) {
		return true;
	}

	FILE* file = fopen(path, "rb");
	if (!file) {
		perror(path);
		return false;
	}

	long len = -1;
	if (fseek(file, 0, SEEK_END) == 0) {
		len = ftell(file);
	}

	bool ok = len >= 0 && len <= INT_MAX && fseek(file, 0, SEEK_SET) == 0;
	if (ok) {
		bytes->data = malloc(len ? (size_t)len : 1);
		ok = bytes->data &&
		     fread(bytes->data, 1, (size_t)len, file) == (size_t)len;
		bytes->len = (size_t)len;
	}

	if (fclose(file) != 0) {
		ok = false;
	}

	if (!ok) {
		fprintf(stderr, "sign: cannot read %s (maximum INT_MAX bytes)\n", path);
		free(bytes->data);
		*bytes = (sign_buffer){0};
	}

	return ok;
}

bool file_write(const char* path, const unsigned char* data, size_t len) {
	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (fd < 0) {
		perror(path);
		return false;
	}

	FILE* file = fdopen(fd, "wb");
	if (!file) {
		close(fd);
		unlink(path);
		return false;
	}

	bool ok = fwrite(data, 1, len, file) == len;
	if (fclose(file) != 0) {
		ok = false;
	}

	if (!ok) {
		perror(path);
		unlink(path);
	}

	return ok;
}
