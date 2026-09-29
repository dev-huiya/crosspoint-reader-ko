#pragma once

#include <cstdint>
#include <string>

// Builds an opened book's cover cache in the background, so the home screen
// only ever draws cached thumbnails (or a placeholder) and never converts
// covers or scans folders itself.
//
// One low-priority task runs one book at a time: the same-name cover image
// recheck (lib/SiblingCover), then the home thumbnail at the height the home
// screen last recorded (APP_STATE.homeCoverThumbHeight), and for TXT the
// cover.bmp used by the sleep screen. Decoder buffers exist only while the
// task runs; the task and its stack are freed when the queue is empty.
namespace cover_job {

// Queues the book (replacing a queued, not yet started one) and starts the
// task if it is idle. Called once the reader has drawn its first page.
void schedule(const std::string& bookPath);

// Stops taking new work and waits up to timeoutMs for a running book to
// finish, before deep sleep or handing the SD card to USB. Returns true when
// the task is idle.
bool pause(uint32_t timeoutMs);

// Allows work again and starts a queued book.
void resume();

}  // namespace cover_job
