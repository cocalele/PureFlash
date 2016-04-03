// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2011 New Dream Network
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include  "/usr/include/errno.h"
#include "asm-generic/errno-base.h"

#include <limits.h>
//#include <sstream>
#include <stdlib.h>
//#include <string>
#include <string.h>

//using std::ostringstream;

long long strict_strtoll(const char *str, int base, char *err)
{
  char *endptr;
  errno = 0; /* To distinguish success/failure after call (see man page) */
  long long ret = strtoll(str, &endptr, base);

  if ((errno == ERANGE && (ret == LLONG_MAX || ret == LLONG_MIN))
      || (errno != 0 && ret == 0)) {
      if (err)
      {
	strcpy(err, "strict_strtoll: integer underflow or overflow parsing '");
	strcat(err, str);
	strcat(err, "'");
      }
      return 0;
  }
  if (endptr == str) {
      if (err)
      {
	strcpy(err, "strict_strtoll: expected integer, got: '");
	strcat(err, str);
	strcat(err, "'");
      }
    return 0;
  }
  if (*endptr != '\0') {
    if (err)
    {
      strcpy(err, "strict_strtoll: garbage at end of string. got: '");
      strcat(err, str);
      strcpy(err, "'");
    }
    return 0;
  }
  if (err)
  {
    err[0] = 0;
  }
  return ret;
}

int strict_strtol(const char *str, int base, char *err)
{
  long long ret = strict_strtoll(str, base, err);
  if (strlen(err) != 0)
    return 0;
  if (ret <= INT_MIN) {
    if (err)
    {
      strcpy(err, "strict_strtol: integer underflow parsing '");
      strcat(err, str);
      strcat(err, "'");
    }
    return 0;
  }
  if (ret >= INT_MAX) {
    if (err)
    {
      strcpy(err, "strict_strtol: integer overflow parsing '");
      strcat(err, str);
      strcat(err, "'");
    }
    return 0;
  }
  if (err)
  {
    err[0] = 0;
  }
  return (int)ret;
}

double strict_strtod(const char *str, char *err)
{
  char *endptr;
  errno = 0; /* To distinguish success/failure after call (see man page) */
  double ret = strtod(str, &endptr);
  if (errno == ERANGE) {
    if (err)
    {
      strcpy(err, "strict_strtod: floating point overflow or underflow parsing '");
      strcat(err, str);
      strcat(err, "'");
    }
    return 0;
  }
  if (err)
  {
    err[0] = 0;
  }
  return ret;
}

float strict_strtof(const char *str, char *err)
{
  char *endptr;
  errno = 0; /* To distinguish success/failure after call (see man page) */
  float ret = strtof(str, &endptr);
  if (errno == ERANGE) {
    if (err)
    {
      strcpy(err, "strict_strtof: floating point overflow or underflow parsing '");
      strcat(err, str);
      strcat(err, "'");
    }
    return 0.0;
  }
  if (endptr == str) {
    if (err)
    {
      strcpy(err, "strict_strtof: expected float, got: '");
      strcat(err, str);
      strcat(err, "'");
    }
    return 0;
  }
  if (*endptr != '\0') {
    if (err)
    {
      strcpy(err, "strict_strtof: garbage at end of string. got: '");
      strcat(err, str);
      strcat(err, "'");
    }
    return 0;
  }
  if (err)
  {
    err[0] = 0;
  }
  return ret;
}
