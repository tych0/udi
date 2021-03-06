/*
 * Copyright (c) 2011-2015, Dan McNulty
 * All rights reserved.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

package net.libudi.api;

import java.nio.file.Path;

/**
 * A bean containing configuration properties for a process under udi control
 *
 * @author mcnulty
 */
public class UdiProcessConfig {

    private Path rootDir;

    /**
     * @return obtains the full path for the root of the UDI pseudo-filesystem to use for controlling the process
     */
    public Path getRootDir() {
        return rootDir;
    }

    /**
     * @param rootDir the full path for the root of the UDI pseudo-filesystem to use for controlling the process
     */
    public void setRootDir(Path rootDir) {
        this.rootDir = rootDir;
    }
}
