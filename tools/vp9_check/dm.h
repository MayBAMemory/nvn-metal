// 只给 tools/test_vp9_vpx.sh 用：在 Linux / Mac 上不带宿主单独编 host/kirby_webm.c、host/kirby_vp9_vpx.c 时顶替上游的 dm.h
// For tools/test_vp9_vpx.sh only: stands in for upstream dm.h when host/kirby_webm.c and host/kirby_vp9_vpx.c are built alone on Linux / Mac without the host
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LOG(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
