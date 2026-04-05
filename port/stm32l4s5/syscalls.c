/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Anbo Peng
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
/* Minimal newlib syscall stubs for bare-metal (suppress linker warnings) */

#include <sys/stat.h>
#include <errno.h>

int _close(int fd)   { (void)fd; errno = EBADF;  return -1; }
int _read(int fd, char *buf, int len)  { (void)fd; (void)buf; (void)len; errno = EBADF; return -1; }
int _write(int fd, const char *buf, int len) { (void)fd; (void)buf; (void)len; errno = EBADF; return -1; }
int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; errno = EBADF; return -1; }
int _fstat(int fd, struct stat *st) { (void)fd; (void)st; errno = EBADF; return -1; }
int _isatty(int fd)  { (void)fd; return 0; }
int _getpid(void)    { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
