
#include "filemq_classes.h"

int main (int argc, char *argv [])
{
    if (argc < 2) {
        puts ("usage: filemq_server publish-from");
        return 0;
    }
    zactor_t *fmq_server = zactor_new (fmq_server, "filemq_server");

    zstr_send (fmq_server, "VERBOSE");
    zstr_sendx (fmq_server, "PUBLISH", argv [1], NULL);
    zstr_sendx (fmq_server, "BIND", "tcp://*:5670", NULL);

    while (!zsys_interrupted)
        zclock_sleep (1000);
    puts ("interrupted");

    zactor_destroy (&fmq_server);
    return 0;
}
