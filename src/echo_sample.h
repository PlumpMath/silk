#ifndef __ECHO_SAMPLE_H__
#define __ECHO_SAMPLE_H__

/*
 * The default port number on which the server listens
 */
#define ECHO_DERVER_DEFAULT_PORT    2222

/*
 * The maximum length of a string to be echoed.
 */
#define MAX_MSG_STRING_LENGTH     254

/*
 * The msg we send over the wire. note we set a buffer for maximum length but
 *  we send only the string.
 */
struct __attribute__((__packed__)) echo_msg_t {
    // a header of the msg
    struct echo_msg_hdr_t {
        //containing the number of bytes following (inc terminating NULL)
        uint16_t    len;
    } hdr;
    char        msg[MAX_MSG_STRING_LENGTH];
};


#endif // __ECHO_SAMPLE_H__
