#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <BaseTsd.h>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <malloc.h>
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <cassert>
#ifdef _MSC_VER
using ssize_t = SSIZE_T;
#define restrict __restrict
struct pollfd;
extern "C" int poll(struct pollfd*, unsigned, int);
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4201 4244 4996)
#endif
#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_aout.h>
#include <vlc_variables.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#ifdef poll
#undef poll
#endif
#ifdef restrict
#undef restrict
#endif
