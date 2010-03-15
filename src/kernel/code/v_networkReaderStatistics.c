/*
 *                         OpenSplice DDS
 *
 *   This software and documentation are Copyright 2006 to 2009 PrismTech 
 *   Limited and its licensees. All rights reserved. See file:
 *
 *                     $OSPL_HOME/LICENSE 
 *
 *   for full copyright notice and license terms. 
 *
 */

#include "v_statistics.h"
#include "v__statistics.h"
#include "v_networkReaderStatistics.h"
#include "v_readerStatistics.h"
#include "v_maxValue.h"
#include "os_report.h"


static c_type networkReaderStatisticsType = NULL;



v_networkReaderStatistics v_networkReaderStatisticsNew(v_kernel k)
{
    v_networkReaderStatistics nrs;

    assert(k != NULL);
    assert(C_TYPECHECK(k, v_kernel));

    if (networkReaderStatisticsType == NULL) {
        networkReaderStatisticsType = c_resolve(c_getBase(k), "kernelModule::v_networkReaderStatistics");
    }
    nrs = v_networkReaderStatistics(v_new(k, networkReaderStatisticsType));
    v_networkReaderStatisticsInit(nrs);
    return nrs;
}

void v_networkReaderStatisticsInit(v_networkReaderStatistics nrs)
{
	v_kernel kernel;
	os_uint32 i;
	assert(nrs != NULL);
    assert(C_TYPECHECK(nrs,v_networkReaderStatistics));
    kernel = v_objectKernel(nrs);
    v_statisticsInit(v_statistics(nrs));
    nrs->queuesCount = 0; /* better to get the actual value from networking */
    //nrs->queues = NULL;
    //for now length is 32

    nrs->queues = c_arrayNew(c_resolve(c_getBase(kernel), "kernelModule::v_networkQueueStatistics"),64);
   /*
    for (i=0;i<32;i++) {
        	nrs->queues[i] = NULL;
        }
        */
}

c_bool v_networkReaderStatisticsReset(v_networkReaderStatistics nrs, const c_char* fieldName)
{
    c_bool result;

    assert(nrs!=NULL);
    assert(C_TYPECHECK(nrs, v_networkReaderStatistics));

    result = FALSE;

    if (fieldName != NULL) {
        result = v_statisticsResetField(v_statistics(nrs), fieldName);
    } else {
        /* Nothing to reset on this level */
    }
    return result;
}

void v_networkReaderStatisticsDeinit(v_networkReaderStatistics nrs)
{
    assert(nrs != NULL);
    assert(C_TYPECHECK(nrs, v_networkReaderStatistics));
}

void v_networkReaderStatisticsFree(v_networkReaderStatistics nrs)
{
    assert(nrs != NULL);
    assert(C_TYPECHECK(nrs, v_networkReaderStatistics));

    v_networkReaderStatisticsDeinit(nrs);
    c_free(nrs);
}


