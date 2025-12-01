/*
 * Copyright (c) 2025 - softworkz
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Resource manager stub implementation
 * Graph functionality has been removed
 */

#include "config.h"
#include "resman.h"
#include "libavutil/dict.h"

// Simple stub implementation - graph resources removed
static AVDictionary *resource_dic = NULL;

void ff_resman_uninit(void)
{
    av_dict_free(&resource_dic);
}

char *ff_resman_get_string(FFResourceId resource_id)
{
    // Stub implementation - returns NULL since no resources are defined
    (void)resource_id;
    return NULL;
}
