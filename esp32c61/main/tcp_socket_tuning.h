#pragma once

/* Call only for a newly created, exclusively owned socket, BEFORE connect().
 * Returns 0 or a positive errno. Never resize an established/buffered PCB. */
int tcp_upload_set_send_buffer(int fd, unsigned bytes, unsigned *actual);
