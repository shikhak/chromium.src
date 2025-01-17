// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from 'chrome://resources/ash/common/assert.js';
import {NativeEventTarget as EventTarget} from 'chrome://resources/ash/common/event_target.js';

import {FileOperationManager} from '../../externs/background/file_operation_manager.js';


/**
 * Mock implementation of {FileOperationManager} for tests.
 * @implements {FileOperationManager}
 */
export class MockFileOperationManager extends EventTarget {
  constructor() {
    super();

    /**
     * Event to be dispatched when requestTaskCancel is called. Note: the
     * unittest writes this value before calling requestTaskCancel().
     * @type {Event}
     */
    // @ts-ignore: error TS2322: Type 'null' is not assignable to type 'Event'.
    this.cancelEvent = null;

    /** @type {!Array<string>} */
    this.generatedTaskIds = [];

    /** @type {Function} */
    // @ts-ignore: error TS2322: Type 'null' is not assignable to type
    // 'Function'.
    this.pasteResolver = null;
  }

  /**
   * Dispatches a cancel event that has been specified by the unittest.
   */
  requestTaskCancel() {
    assert(this.cancelEvent);
    this.dispatchEvent(this.cancelEvent);
  }

  /**
   * Kick off pasting.
   *
   * @param {Array<Entry>} sourceEntries Entries of the source files.
   * @param {DirectoryEntry} targetEntry The destination entry of the target
   *     directory.
   * @param {boolean} isMove True if the operation is "move", otherwise (i.e.
   *     if the operation is "copy") false.
   * @param {string=} opt_taskId If the corresponding item has already created
   *     at another places, we need to specify the ID of the item. If the
   *     item is not created, FileOperationManager generates new ID.
   */
  paste(sourceEntries, targetEntry, isMove, opt_taskId) {
    if (this.pasteResolver) {
      this.pasteResolver.call(this, {
        sourceEntries: sourceEntries,
        targetEntry: targetEntry,
        isMove: isMove,
        opt_taskId: opt_taskId,
      });
      // Reset the resolver for the next paste call.
      // @ts-ignore: error TS2322: Type 'null' is not assignable to type
      // 'Function'.
      this.pasteResolver = null;
    }
  }

  /**
   * @return {Promise<Object>} A promise that is resolved the next time #paste
   * is called.  The Object contains the arguments that #paste was called with.
   */
  whenPasteCalled() {
    if (this.pasteResolver) {
      throw new Error('Only one paste call can be waited on at a time.');
    }

    // @ts-ignore: error TS6133: 'reject' is declared but its value is never
    // read.
    return new Promise((resolve, reject) => {
      this.pasteResolver = resolve;
    });
  }

  /**
   * Generates a unique task Id.
   * @return {string}
   */
  generateTaskId() {
    const newTaskId = 'task' + this.generatedTaskIds.length;
    this.generatedTaskIds.push(newTaskId);
    return newTaskId;
  }

  /**
   * @return {boolean} Whether or not the given task ID belongs to a task
   *     generated by this manager.
   */
  // @ts-ignore: error TS7006: Parameter 'id' implicitly has an 'any' type.
  isKnownTaskId(id) {
    return this.generatedTaskIds.indexOf(id) !== -1;
  }

  /** @return {boolean} */
  hasQueuedTasks() {
    throw new Error('Not implemented');
  }
  // @ts-ignore: error TS2355: A function whose declared type is neither 'void'
  // nor 'any' must return a value.
  /** @return {Promise} */
  // @ts-ignore: error TS7006: Parameter 'isMove' implicitly has an 'any' type.
  filterSameDirectoryEntry(sourceEntries, targetEntry, isMove) {}
  /** @return {boolean} */
  // @ts-ignore: error TS7006: Parameter 'entries' implicitly has an 'any' type.
  willUseTrash(volumeManager, entries) {
    throw new Error('Not implemented');
  }
  deleteEntries() {}
  setFileManager() {}
  notifyExtractDone() {}
  handleMissingPassword() {}
  /** @return {!Promise<!FileEntry>} */
  // @ts-ignore: error TS7006: Parameter 'destination' implicitly has an 'any'
  // type.
  async writeFile(file, destination) {
    throw new Error('Not implemented');
  }
}
