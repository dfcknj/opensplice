/*
 *                         OpenSplice DDS
 *
 *   This software and documentation are Copyright 2006 to TO_YEAR PrismTech
 *   Limited, its affiliated companies and licensors. All rights reserved.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 */
package org.opensplice.cm.qos.holder;

import org.opensplice.cm.qos.SubscriberQoS;

public class SubscriberQosHolder extends QosHolder implements Comparable<SubscriberQosHolder> {
    private SubscriberQoS sq = null;

    public SubscriberQosHolder(String name, SubscriberQoS sq) {
        super(name);
        this.sq = sq;
    }

    public SubscriberQoS getQos() {
        return sq;
    }

    @Override
    public int compareTo(SubscriberQosHolder sqh) {
        return this.toString().compareTo(sqh.toString());
    }
}
