/*  =========================================================================
    fmq_client - FILEMQ Client

    Copyright (c) the Contributors as noted in the AUTHORS file.       
    This file is part of FileMQ, a C implemenation of the protocol:    
    https://github.com/danriegsecker/filemq2.                          
                                                                       
    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.           
    =========================================================================
*/

/*
@header
    Description of class for man page.
@discuss
    Detailed discussion of the class, if any.
@end
*/

//  TODO: Change these to match your project's needs
#include "../include/fmq_msg.h"
#include "../include/fmq_client.h"

//  Forward reference to method arguments structure
typedef struct _client_args_t client_args_t;

//  Additional forward declarations
typedef struct _sub_t sub_t;

//  There's no point making these configurable
#define CREDIT_SLICE    1000000
#define CREDIT_MINIMUM  (CREDIT_SLICE * 4) + 1

//  This structure defines the context for a client connection
typedef struct {
    //  These properties must always be present in the client_t
    //  and are set by the generated engine. The cmdpipe gets
    //  messages sent to the actor; the msgpipe may be used for
    //  faster asynchronous message flows.
    zsock_t *cmdpipe;           //  Command pipe to/from caller API
    zsock_t *msgpipe;           //  Message pipe to/from caller API
    zsock_t *dealer;            //  Socket to talk to server
    fmq_msg_t *message;         //  Message to/from server
    client_args_t *args;        //  Arguments from methods
    
    //  TODO: Add specific properties for your application
    size_t credit;              //  Current credit pending
    zfile_t *file;              //  File we're currently writing
    char *inbox;                //  Path where files will be stored
    zlist_t *subs;              //  Our subscriptions
} client_t;

//  Include the generated client engine
#include "fmq_client_engine.inc"

//  Subscription in memory
struct _sub_t {
    client_t *client;           //  Pointer to parent client
    char *inbox;                //  Inbox location
    char *path;                 //  Path we subscribe to
};

static sub_t *
sub_new (client_t *client, char *inbox, char *path)
{
    sub_t *self = (sub_t *) zmalloc (sizeof (sub_t));
    self->client = client;
    self->inbox = strdup (inbox);
    self->path = strdup (path);
    return self;
}

static void
sub_destroy (sub_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        sub_t *self = *self_p;
        free (self->inbox);
        free (self->path);
        free (self);
        *self_p = NULL;
    }
}

//  Allocate properties and structures for a new client instance.
//  Return 0 if OK, -1 if failed

static int
client_initialize (client_t *self)
{
    zsys_info ("client is initializing");
    self->subs = zlist_new ();
    self->credit = 0;
    self->inbox = NULL;
    return 0;
}

//  Free properties and structures for a client instance

static void
client_terminate (client_t *self)
{
    //  Destroy properties here
    zsys_info ("client is terminating");
    while (zlist_size (self->subs)) {
        sub_t *sub = (sub_t *) zlist_pop (self->subs);
        sub_destroy (&sub);
    }
    zlist_destroy (&self->subs);
}




//  ---------------------------------------------------------------------------
//  connect_to_server_endpoint
//

static void
connect_to_server_endpoint (client_t *self)
{
    if (zsock_connect (self->dealer, "%s", self->args->endpoint)) {
        engine_set_exception (self, error_event);
        zsys_warning ("could not connect to %s", self->args->endpoint);
    }
}


//  ---------------------------------------------------------------------------
//  use_connect_timeout
//

static void
use_connect_timeout (client_t *self)
{
    engine_set_timeout (self, self->args->timeout);
}


//  ---------------------------------------------------------------------------
//  connected_to_server
//

static void
connected_to_server (client_t *self)
{
    zsys_debug ("connected to server");
    zsock_send (self->cmdpipe, "si", "SUCCESS", 0);
}


//  ---------------------------------------------------------------------------
//  format_icanhaz_command
//

static void
format_icanhaz_command (client_t *self)
{

}


//  ---------------------------------------------------------------------------
//  process_the_patch
//

static void
process_the_patch (client_t *self)
{

}


//  ---------------------------------------------------------------------------
//  refill_credit_as_needed
//

static void
refill_credit_as_needed (client_t *self)
{
    zsys_debug ("refill credit as needed");
    size_t credit_to_send = 0;
    while (self->credit < CREDIT_MINIMUM) {
        credit_to_send += CREDIT_SLICE;
        self->credit += CREDIT_SLICE;
    }
    if (credit_to_send) {
        fmq_msg_set_credit (self->message, credit_to_send);
        engine_set_next_event (self, send_credit_event);
    }
}


//  ---------------------------------------------------------------------------
//  log_access_denied
//

static void
log_access_denied (client_t *self)
{
    zsys_warning ("access was denied");
}


//  ---------------------------------------------------------------------------
//  log_invalid_message
//

static void
log_invalid_message (client_t *self)
{
    zsys_error ("invalid message");
    fmq_msg_print (self->message);
}


//  ---------------------------------------------------------------------------
//  log_protocol_error
//

static void
log_protocol_error (client_t *self)
{
    zsys_error ("protocol error");
    fmq_msg_print (self->message);
}


//  ---------------------------------------------------------------------------
//  signal_server_not_present
//

static void
signal_server_not_present (client_t *self)
{
    zsock_send (self->cmdpipe, "sis", "FAILURE", -1, "server is not reachable");
}


//  ---------------------------------------------------------------------------
//  setup_inbox
//

static void
setup_inbox (client_t *self)
{
    if (!self->inbox) {
        self->inbox = strdup (self->args->path);
        zsock_send (self->cmdpipe, "si", "SUCCESS", 0);
    }
    else
        zsock_send (self->cmdpipe, "sis", "FAILURE", -1, "inbox already set");
}


//  ---------------------------------------------------------------------------
//  Selftest

void
fmq_client_test (bool verbose)
{
    printf (" * fmq_client: ");
    if (verbose)
        printf ("\n");
    
    //  @selftest
    zactor_t *client = zactor_new (fmq_client, NULL);
    if (verbose)
        zstr_send (client, "VERBOSE");
    zactor_destroy (&client);
    //  @end
    printf ("OK\n");
}


//  ---------------------------------------------------------------------------
//  signal_subscribe_success
//

static void
signal_subscribe_success (client_t *self)
{

}
