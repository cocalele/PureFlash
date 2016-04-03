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

#ifndef __S5_STRTOL_H__
#define __S5_STRTOL_H__

//#include <string>

long long strict_strtoll(const char *str, int base, char *err);

int strict_strtol(const char *str, int base, char *err);

double strict_strtod(const char *str, char *err);

float strict_strtof(const char *str, char *err);

#endif
