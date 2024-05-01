/* Copyright (c) 2024 Elliot Nunn */
/* Licensed under the MIT license */

// Reasons to rewrite all these functions for the DRVR runtime:
// 1. Compile with -msep-data so the code can go in ROM
// 2. Break the dependence on nuisance soft-div library functions
// 3. Use the Mac Toolbox where possible

#include <stddef.h>
#include <string.h>
#include <Memory.h>

// Searches for the first occurrence of the character c (an unsigned char) in the first n bytes of the string pointed to, by the argument str.
void *memchr(const void *str, int c, size_t n) {
  const unsigned char *src = str;

	while (n--) {
		if (*src == c) return (void *)src;
		src++;
	}

	return NULL;
}

// Compares the first n bytes of str1 and str2.
int memcmp(const void *str1, const void *str2, size_t n) {
	const unsigned char *s1 = str1;
	const unsigned char *s2 = str2;

	while (n--) {
		if (*s1 != *s2) return *s1 - *s2;
		s1++;
		s2++;
	}

	return 0;
}

// Copies n characters from src to dest.
void *memcpy(void *dest, const void *src, size_t n) {
	BlockMoveData(src, dest, n);
	return dest;
}

// Another function to copy n characters from str2 to str1.
void *memmove(void *dest, const void *src, size_t n) {
	BlockMoveData(src, dest, n);
	return dest;
}

// Copies the character c (an unsigned char) to the first n characters of the string pointed to, by the argument str.
void *memset(void *str, int c, size_t n) {
	char *s = str;
	while (n--) *s++ = c;
}

// Appends the string pointed to, by src to the end of the string pointed to by dest.
char *strcat(char *dest, const char *src) {
	strcpy(dest + strlen(dest), src);
	return dest;
}

// Searches for the first occurrence of the character c (an unsigned char) in the string pointed to, by the argument str.
char *strchr(const char *str, int c) {
	for (;;) {
		if (*str == c) return (void *)str;
		else if (*str == 0) return NULL;
		else str++;
	}
}

// Compares the string pointed to, by str1 to the string pointed to by str2.
int strcmp(const char *str1, const char *str2) {
	while (*str1 != 0 && *str1 == *str2) {
		str1++;
		str2++;
	}

	return (*(unsigned char *)str1) - (*(unsigned char *)str2);
}

// Copies the string pointed to, by src to dest.
char *strcpy(char *dest, const char *src) {
	size_t len = 0;
	do {
		dest[len] = src[len];
	} while (src[len++]);
	return dest;
}

// Copies the string pointed to, by src to dest, returning pointer to final null
char *stpcpy(char *dest, const char *src) {
	size_t len = 0;
	do {
		dest[len] = src[len];
	} while (src[len++]);
	return dest + len;
}

// Computes the length of the string str up to but not including the terminating null character.
size_t strlen(const char *str) {
	size_t len = 0;
	while (str[len]) len++;
	return len;
}
