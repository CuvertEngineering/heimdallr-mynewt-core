/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#include "fcb_test.h"

TEST_CASE(fcb_test_append)
{
    int rc;
    struct fcb *fcb;
    struct fcb_entry loc;
    uint8_t test_data[128];
    int i;
    int j;
    int var_cnt;

    fcb = &test_fcb;

    for (i = 1; i < sizeof(test_data); i++) {
        for (j = 0; j < i; j++) {
            test_data[j] = fcb_test_append_data(i, j);
        }
        rc = fcb_append(fcb, i, &loc);
        TEST_ASSERT_FATAL(rc == 0);
        rc = fcb_write(&loc, 0, test_data, i);
        TEST_ASSERT(rc == 0);
        rc = fcb_append_finish(&loc);
        TEST_ASSERT(rc == 0);
    }

    var_cnt = 1;
    rc = fcb_walk(fcb, 0, fcb_test_data_walk_cb, &var_cnt);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(var_cnt == sizeof(test_data));
}
