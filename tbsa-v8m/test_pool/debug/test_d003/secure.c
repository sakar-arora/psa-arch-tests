/** @file
 * Copyright (c) 2018, Arm Limited or its affiliates. All rights reserved.
 * SPDX-License-Identifier : Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
**/

#include "val_test_common.h"
#include "val_target.h"
#include "val_infra.h"
#include "val_pe.h"
#include "val_debug.h"

tbsa_val_api_t        *g_val;
dpm_desc_t            *dpm_desc;

#define TEST_DATA   0x12DEED21

/*  Publish these functions to the external world as associated to this test ID */
TBSA_TEST_PUBLISH(CREATE_TEST_ID(TBSA_DEBUG_BASE, 3),
                  CREATE_TEST_TITLE("All DPMs must implement a fuse-controlled Closed State"),
                  CREATE_REF_TAG("R050_TBSA_DEBUG"),
                  entry_hook,
                  test_payload,
                  exit_hook);

void entry_hook(tbsa_val_api_t *val)
{
    tbsa_test_init_t init = {
                             .bss_start      = &__tbsa_test_bss_start__,
                             .bss_end        = &__tbsa_test_bss_end__
                            };

    val->test_initialize(&init);

    g_val = val;

    val->set_status(RESULT_PASS(TBSA_STATUS_SUCCESS));
}

tbsa_status_t test_env_reset(void)
{
    tbsa_status_t status;
    /*Signal the debugger that the test is complete*/
    status = g_val->debug_set_status(DBG_INIT, SEQ_NEXT_TEST);
    if (g_val->err_check_set(TEST_CHECKPOINT_F, status)) {
        return status;
    }
     return TBSA_STATUS_SUCCESS;
}

tbsa_status_t test_dbg_seq_write(uint32_t addr, dbg_seq_status_t seq_status)
{
    tbsa_status_t status;
    /*Wait for the debugger to be ready and then write the data to used by debugger*/
    status = g_val->debug_get_status(DBG_WRITE);
    if (g_val->err_check_set(TEST_CHECKPOINT_10, status)) {
        return status;
    }
    g_val->mem_write((uint32_t *)dpm_desc->data_addr, WORD, addr);

    status = g_val->debug_set_status(DBG_WRITE, seq_status);
    if (g_val->err_check_set(TEST_CHECKPOINT_11, status)) {
        return status;
    }
    return TBSA_STATUS_SUCCESS;
}

tbsa_status_t test_dbg_seq_read(uint32_t *addr, dbg_seq_status_t seq_status)
{
    tbsa_status_t status;
    /*Wait for the debugger to be ready and then read the data returned by debugger*/
    status = g_val->debug_get_status(DBG_READ);
    if (g_val->err_check_set(TEST_CHECKPOINT_12, status)) {
        return status;
    }
    g_val->mem_read((uint32_t *)dpm_desc->data_addr, WORD, addr);

    status = g_val->debug_set_status(DBG_READ, seq_status);
    if (g_val->err_check_set(TEST_CHECKPOINT_13, status)) {
        return status;
    }
    return TBSA_STATUS_SUCCESS;
}

void test_payload(tbsa_val_api_t *val)
{
    tbsa_status_t status;
    uint32_t      data, dpm_instance, timeout, dpm_lock, dpm_enable, dpm_status;
    uint32_t      region_num = 0, instance = 0, minor_id = 1, region_num_inst, s_addr=0, ns_addr=0;
    memory_hdr_t  *memory_hdr;
    memory_desc_t *memory_desc;
    dpm_hdr_t     *dpm_hdr;

    status = val->target_get_config(TARGET_CONFIG_CREATE_ID(GROUP_DPM, 0, 0),
                                    (uint8_t **)&dpm_hdr, (uint32_t *)sizeof(dpm_hdr_t));
    if (val->err_check_set(TEST_CHECKPOINT_1, status)) {
        return;
    }

    /* Check if DPM is present.*/
    if (!dpm_hdr->num) {
        val->print(PRINT_ERROR, "\nNo DPM present in the platform", 0);
        val->err_check_set(TEST_CHECKPOINT_2, TBSA_STATUS_NOT_FOUND);
        return;
    }

    /* Check if the debugger is connected, by checking for the init data.*/
    status = val->debug_get_status(DBG_INIT);
    if (val->err_check_set(TEST_CHECKPOINT_3, status)) {
        if (status == TBSA_STATUS_SKIP)
            val->set_status(RESULT_SKIP(1));
        return;
    }

    /* Check the access on addresses controlled by closed DPM */
    for (dpm_instance = 0; dpm_instance < dpm_hdr->num; dpm_instance++) {
        status = val->target_get_config(TARGET_CONFIG_CREATE_ID(GROUP_DPM, DPM_DPM, dpm_instance),
                                        (uint8_t **)&dpm_desc, (uint32_t *)sizeof(dpm_desc_t));
        if (val->err_check_set(TEST_CHECKPOINT_4, status)) {
            goto clean_up;
        }

        /* Get the current state of the DPM under check*/
        status = val->dpm_get_state(dpm_desc->index, &dpm_status);
        if (val->err_check_set(TEST_CHECKPOINT_5, status)) {
            goto clean_up;
        }

        dpm_lock = DPM_LOCK_IMPLEMENTED|DPM_LOCK_VALUE;
        dpm_enable = DPM_EN_IMPLEMENTED|DPM_EN_VALUE;

        if ((dpm_status & dpm_lock) == dpm_lock)
            continue;
        else if ((dpm_status & dpm_enable) == dpm_enable) {
            /* Set the DPM state to Open.*/
            status = val->dpm_set_state(dpm_desc->index, DPM_OPEN_STATE, dpm_desc->unlock_token);
            if (val->err_check_set(TEST_CHECKPOINT_6, status)) {
                goto clean_up;
            }
        }

        status = val->target_get_config(TARGET_CONFIG_CREATE_ID(GROUP_MEMORY, 0, 0),
                              (uint8_t **)&memory_hdr, (uint32_t *)sizeof(memory_hdr_t));
        if (val->err_check_set(TEST_CHECKPOINT_7, status)) {
            goto clean_up;
        }
        /*Check for R/W address controlled by DPM under check, and access*/
        for (region_num = 0; region_num < memory_hdr->num; region_num++) {
            instance = 0;
            do {
                status = val->target_get_config(TARGET_CONFIG_CREATE_ID(GROUP_MEMORY, minor_id, instance),
                                      (uint8_t **)&memory_desc, (uint32_t *)sizeof(memory_desc_t));
                if (status == TBSA_STATUS_NOT_FOUND) {
                    break;
                } else if (val->err_check_set(TEST_CHECKPOINT_8, status)) {
                    goto clean_up;
                }
                region_num_inst = GET_NUM_INSTANCE(memory_desc);

                if ((memory_desc->dpm_index == dpm_desc->index) && (memory_desc->mem_type == TYPE_NORMAL_READ_WRITE)) {
                    if (memory_desc->attribute == MEM_NONSECURE)
                        ns_addr++;
                    else
                        s_addr++;
                    /*Initiliaze the memory with known data*/
                    val->mem_write((uint32_t *)memory_desc->start, WORD, TEST_DATA);

                    if (test_dbg_seq_write(memory_desc->start, SEQ_CLOSED_STATE_READ))
                        goto clean_up;

                    /* Set the DPM state to Closed.*/
                    status = val->dpm_set_state(dpm_desc->index, DPM_CLOSED_STATE, 0);
                    if (val->err_check_set(TEST_CHECKPOINT_9, status)) {
                        goto clean_up;
                    }
                    /* Wait until debugger has completed the access whilst DPM is in Closed state.*/
                    timeout = 0x1000000;
                    while(--timeout);

                    /* Set the DPM state to Open.*/
                    status = val->dpm_set_state(dpm_desc->index, DPM_OPEN_STATE, dpm_desc->unlock_token);
                    if (val->err_check_set(TEST_CHECKPOINT_A, status)) {
                        goto clean_up;
                    }
                    /* Read the data returned by debugger and compare to get the results.*/
                    if(test_dbg_seq_read(&data, SEQ_CLOSED_STATE_READ))
                        goto clean_up;

                    if (data == TEST_DATA) {
                        val->err_check_set(TEST_CHECKPOINT_B, TBSA_STATUS_ERROR);
                        val->print(PRINT_ERROR, "\nDPM could not restrict access in Closed State", 0);
                        val->print(PRINT_ERROR, "\nDebugger read the actual data = 0x%x", TEST_DATA);
                        val->print(PRINT_ERROR, " at address = 0x%x", memory_desc->start);
                        goto clean_up;
                    }
                    /*Initiliaze the memory with known data*/
                    val->mem_write((uint32_t *)memory_desc->start, WORD, ~TEST_DATA);

                    if (test_dbg_seq_write(memory_desc->start, SEQ_CLOSED_STATE_WRITE))
                        goto clean_up;

                    if (test_dbg_seq_write(TEST_DATA, SEQ_CLOSED_STATE_WRITE))
                        goto clean_up;

                    /* Set the DPM state to Closed.*/
                    status = val->dpm_set_state(dpm_desc->index, DPM_CLOSED_STATE, 0);
                    if (val->err_check_set(TEST_CHECKPOINT_C, status)) {
                        goto clean_up;
                    }
                    /* Wait until debugger has completed the access whilst DPM is in Closed state.*/
                    timeout = 0x1000000;
                    while(--timeout);

                    /* Set the DPM state to Open.*/
                    status = val->dpm_set_state(dpm_desc->index, DPM_OPEN_STATE, dpm_desc->unlock_token);
                    if (val->err_check_set(TEST_CHECKPOINT_D, status)) {
                        goto clean_up;
                    }
                    /* Read the data returned by debugger and compare to get the results.*/
                    if(test_dbg_seq_read(&data, SEQ_CLOSED_STATE_WRITE))
                        goto clean_up;

                    val->mem_read((uint32_t *)memory_desc->start, WORD, &data);

                    if (data != ~TEST_DATA) {
                        val->err_check_set(TEST_CHECKPOINT_E, TBSA_STATUS_ERROR);
                        val->print(PRINT_ERROR, "\nDPM could not restrict access in Closed State", 0);
                        val->print(PRINT_ERROR, "\nDebugger updated the data at address = 0x%x", memory_desc->start);
                        goto clean_up;
                    }
                }
                instance++;
            } while (instance < region_num_inst);
            minor_id++;
            if (status == TBSA_STATUS_NOT_FOUND)
                region_num -= 1;
            else
                region_num += region_num_inst - 1;
        }
    }

    if (!ns_addr) {
        val->print(PRINT_WARN, "\n        No non-secure address were accessed", 0);
    }
    if (!s_addr) {
        val->print(PRINT_WARN, "\n        No secure address were accessed", 0);
    }

    if (test_env_reset())
    return;

    val->set_status(RESULT_PASS(TBSA_STATUS_SUCCESS));
    return;

clean_up:
    if (test_env_reset())
    return;
}

void exit_hook(tbsa_val_api_t *val)
{
}
