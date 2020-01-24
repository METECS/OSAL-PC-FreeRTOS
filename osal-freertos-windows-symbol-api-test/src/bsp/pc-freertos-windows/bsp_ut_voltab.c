/*
 ** File   : bsp_voltab.c
 **
 ** Author : Jonathan C. Brandenburg
 **
 ** BSP Volume table for file systems.
 **
 ** Based on src/bsp/pc-rtems/ut-src/bsp_ut_voltab.c with the following license terms:
 **      This is governed by the NASA Open Source Agreement and may be used,
 **      distributed and modified only pursuant to the terms of that agreement.
 **
 **      Copyright (c) 2004-2006, United States government as represented by the
 **      administrator of the National Aeronautics Space Administration.
 **      All rights reserved.
 */

/****************************************************************************************
                                    INCLUDE FILES
 ****************************************************************************************/
#include "common_types.h"
#include "osapi.h"

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

/* 
 **  volume table.
 */
OS_VolumeInfo_t OS_VolumeTable [NUM_TABLE_ENTRIES] = {
		/* Dev Name  Phys Dev   Vol Type  Volatile? Free? IsMounted? Volname MountPt BlockSz */
		{"/ramdev0", "/ram", RAM_DISK, TRUE,     TRUE, FALSE,     " ",    " ",    0        },
		{"/ramdev1", "/drive1", RAM_DISK, TRUE,     TRUE, FALSE,     " ",    " ",    0        },
		{"/ramdev2", "/drive2", RAM_DISK, TRUE,     TRUE, FALSE,     " ",    " ",    0        },
		{"/ramdev3", "/drive3", RAM_DISK, TRUE,     TRUE, FALSE,     " ",    " ",    0        },
		{"/ramdev4", "/drive4", RAM_DISK, TRUE,     TRUE, FALSE,     " ",    " ",    0        },
		{"/ramdev5", "/drive5", RAM_DISK, TRUE,     TRUE, FALSE,     " ",    " ",    0        },
		{"/fsdev0",  "./fs0",   FS_BASED, TRUE,     TRUE, FALSE,     " ",    " ",    0        },
		{"/fsdev1",  "./fs1",   FS_BASED, TRUE,     TRUE, FALSE,     " ",    " ",    0        },
		{"unused",   "unused",  RAM_DISK, TRUE,     TRUE, FALSE,     " ",    " ",    0        },
		{"unused",   "unused",  RAM_DISK, TRUE,     TRUE, FALSE,     " ",    " ",    0        },
		{"unused",   "unused",  RAM_DISK, TRUE,     TRUE, FALSE,     " ",    " ",    0        },
		{"unused",   "unused",  RAM_DISK, TRUE,     TRUE, FALSE,     " ",    " ",    0        },
		{"unused",   "unused",  RAM_DISK, TRUE,     TRUE, FALSE,     " ",    " ",    0        },
		{"unused",   "unused",  RAM_DISK, TRUE,     TRUE, FALSE,     " ",    " ",    0        }
};
