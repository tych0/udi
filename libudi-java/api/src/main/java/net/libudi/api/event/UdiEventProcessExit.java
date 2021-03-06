/*
 * Copyright (c) 2011-2015, Dan McNulty
 * All rights reserved.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

package net.libudi.api.event;

/**
 * An interface for a UdiEvent of type PROCESS_EXIT
 *
 * @author mcnulty
 */
public interface UdiEventProcessExit extends UdiEvent {

    /**
     * @return the exit code for the process exit
     */
    int getExitCode();
}
