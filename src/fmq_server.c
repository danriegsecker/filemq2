/*  =========================================================================
    fmq_server - fmq_server

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
#include "filemq_classes.h"

//  ---------------------------------------------------------------------------
//  Forward declarations for the two main classes we use here

typedef struct _server_t server_t;
typedef struct _client_t client_t;

//  Additional forward declarations
typedef struct _sub_t sub_t;
typedef struct _mount_t mount_t;

//  There's no point making these configurable
#define CHUNK_SIZE      1000000

//  This structure defines the context for each running server. Store
//  whatever properties and structures you need for the server.

struct _server_t {
    //  These properties must always be present in the server_t
    //  and are set by the generated engine; do not modify them!
    zsock_t *pipe;              //  Actor pipe back to caller
    zconfig_t *config;          //  Current loaded configuration
    
    //  TODO: Add any properties you need here
    zlist_t *mounts;            //  Mount points
};

//  ---------------------------------------------------------------------------
//  This structure defines the state for each client connection. It will
//  be passed to each action in the 'self' argument.

struct _client_t {
    //  These properties must always be present in the client_t
    //  and are set by the generated engine; do not modify them!
    server_t *server;           //  Reference to parent server
    fmq_msg_t *message;         //  Message in and out

    //  TODO: Add specific properties for your application
    uint64_t credit;            //  Credit remaining
    zlist_t *patches;           //  Patches to send
    zdir_patch_t *patch;        //  Current patch
    zfile_t *file;              //  Current file we're sending
    off_t offset;               //  Offset of next read in file
    uint64_t sequence;          //  Sequence number for chunck
};

//  Include the generated server engine
#include "fmq_server_engine.inc"

//  ---------------------------------------------------------------------------
//  Subscription object

struct _sub_t {
    client_t *client;           //  Always refers to live client
    char *path;                 //  Path client is subscribed to
    zhash_t *cache;             //  Client's cache list
};

//  --------------------------------------------------------------------------
//  Constructor

static sub_t *
sub_new (client_t *client, const char *path, zhash_t *cache)
{
    sub_t *self = (sub_t *) zmalloc (sizeof (sub_t));
    self->client = client;
    self->path = strdup (path);
    self->cache = zhash_dup (cache);

    //  Cached filenames may be local, in which case prefix them with
    //  the subscription path so we can do a consistent match.

    sub_t *cache_item = (sub_t *) zhash_first (self->cache);
    while (cache_item) {
        char *key = (char *) zhash_cursor (self->cache);
        if (*key != '/') {
            size_t new_key_len = strlen (self->path) + strlen (key) + 2;
            char *new_key = (char *) calloc (new_key_len, sizeof (char));
            snprintf (new_key, new_key_len, "%s/%s", self->path, key);
            zsys_debug ("new_key=%s", new_key);
            zhash_rename (self->cache, key, new_key);
            free (new_key);
        }
        cache_item = (sub_t *) zhash_next (self->cache);
    }
    return self;
}

//  --------------------------------------------------------------------------
//  Destructor

static void
sub_destroy (sub_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        sub_t *self = *self_p;
        zhash_destroy (&self->cache);
        free (self->path);
        free (self);
        *self_p = NULL;
    }
}


//  --------------------------------------------------------------------------
//  Add patch to sub client patches list

static void
sub_patch_add (sub_t *self, zdir_patch_t *patch)
{
    //  Skip file creation if client already has identical file
    zdir_patch_digest_set (patch);
    if (zdir_patch_op (patch) == patch_create) {
        char *digest = (char *) zhash_lookup (self->cache,
                                    zdir_patch_vpath (patch) + strlen(self->path) + 1);
        if (digest && streq (digest, zdir_patch_digest (patch)))
            return;             //  Just skip patch for this client
    }
    //  Remove any previous patches for the same file
    zdir_patch_t *existing = (zdir_patch_t *) zlist_first (self->client->patches);
    while (existing) {
        if (streq (zdir_patch_vpath (patch), zdir_patch_vpath (existing))) {
            zlist_remove (self->client->patches, existing);
            zdir_patch_destroy (&existing);
            break;
        }
        existing = (zdir_patch_t *) zlist_next (self->client->patches);
    }
    if (zdir_patch_op (patch) == patch_create)
        zhash_insert (self->cache,
            zdir_patch_digest (patch), zdir_patch_vpath (patch));
    //  Track that we've queued patch for client, so we don't do it twice
    zlist_append (self->client->patches, zdir_patch_dup (patch));
}

//  --------------------------------------------------------------------------
//  Mount point in memory

struct _mount_t {
    char *location;         //  Physical location
    char *alias;            //  Alias into our tree
    zdir_t *dir;            //  Directory snapshot
    zlist_t *subs;          //  Client subscriptions
};

//  --------------------------------------------------------------------------
//  Constructor
//  Loads directory tree if possible

static mount_t *
mount_new (char *location, char *alias)
{
    //  Mount path must start with '/'
    //  We'll do better error handling later
    assert (*alias == '/');
    
    mount_t *self = (mount_t *) zmalloc (sizeof (mount_t));
    self->location = strdup (location);
    self->alias = strdup (alias);
    self->dir = zdir_new (self->location, NULL);
    self->subs = zlist_new ();
    return self;
}


//  --------------------------------------------------------------------------
//  Destructor

static void
mount_destroy (mount_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        mount_t *self = *self_p;
        free (self->location);
        free (self->alias);
        //  Destroy subscriptions
        while (zlist_size (self->subs)) {
            sub_t *sub = (sub_t *) zlist_pop (self->subs);
            sub_destroy (&sub);
        }
        zlist_destroy (&self->subs);
        zdir_destroy (&self->dir);
        free (self);
        *self_p = NULL;
    }
}


//  --------------------------------------------------------------------------
//  Reloads directory tree and returns true if activity, false if the same

static bool
mount_refresh (mount_t *self, server_t *server)
{
    bool activity = false;

    //  Get latest snapshot and build a patches list for any changes
    zdir_t *latest = zdir_new (self->location, NULL);
    zlist_t *patches = zdir_diff (self->dir, latest, self->alias);

    //  Drop old directory and replace with latest version
    zdir_destroy (&self->dir);
    self->dir = latest;

    //  Copy new patches to clients' patches list
    sub_t *sub = (sub_t *) zlist_first (self->subs);
    while (sub) {
        zdir_patch_t *patch = (zdir_patch_t *) zlist_first (patches);
        while (patch) {
            sub_patch_add (sub, patch);
            patch = (zdir_patch_t *) zlist_next (patches);
            activity = true;
        }
        sub = (sub_t *) zlist_next (self->subs);
    }
    
    //  Destroy patches, they've all been copied
    while (zlist_size (patches)) {
        zdir_patch_t *patch = (zdir_patch_t *) zlist_pop (patches);
        zdir_patch_destroy (&patch);
    }
    zlist_destroy (&patches);
    return activity;
}


//  --------------------------------------------------------------------------
//  Store subscription for mount point

static void
mount_sub_store (mount_t *self, client_t *client, fmq_msg_t *request)
{
    assert (self);
    assert (self->subs);

    //  Store subscription along with any previous ones
    //  Coalesce subscriptions that are on same path
    const char *path = fmq_msg_path (request);
    sub_t *sub = (sub_t *) zlist_first (self->subs);
    while (sub) {
        if (client == sub->client) {
            //  If old subscription is superset/same as new, ignore new
            if (strncmp (path, sub->path, strlen (sub->path)) == 0) {
                zsys_debug ("new subscription already exists");
                return;
            }
            else
            //  If new subscription is superset of old one, remove old
            if (strncmp (sub->path, path, strlen (path)) == 0) {
                zsys_debug ("superset, sub->path=%s, path=%s",
                    sub->path, path);
                zlist_remove (self->subs, sub);
                sub_destroy (&sub);
                sub = (sub_t *) zlist_first (self->subs);
            }
            else
                sub = (sub_t *) zlist_next (self->subs);
        }
        else
            sub = (sub_t *) zlist_next (self->subs);
    }
    //  New subscription for this client, append to our list
    sub = sub_new (client, path, fmq_msg_cache (request));
    zlist_append (self->subs, sub);

    //  If client requested resync, send full mount contents now
    /*
    if (fmq_msg_options_number (client->message, "RESYNC", 0) == 1) {
        zlist_t *patches = zdir_resync (self->dir, self->alias);
        while (zlist_size (patches)) {
            zdir_patch_t *patch = (zdir_patch_t *) zlist_pop (patches);
            sub_patch_add (sub, patch);
            zdir_patch_destroy (&patch);
        }
        zlist_destroy (&patches);
    }
    */
}


//  --------------------------------------------------------------------------
//  Purge subscriptions for a specified client

static void
mount_sub_purge (mount_t *self, client_t *client)
{
    sub_t *sub = (sub_t *) zlist_first (self->subs);
    while (sub) {
        if (sub->client == client) {
            sub_t *next = (sub_t *) zlist_next (self->subs);
            zlist_remove (self->subs, sub);
            sub_destroy (&sub);
            sub = next;
        }
        else
            sub = (sub_t *) zlist_next (self->subs);
    }
}

static int
monitor_the_server (zloop_t *loop, int timer_id, void *arg)
{
    server_t *self = (server_t *) arg;
    bool activity = false;
    mount_t *mount = (mount_t *) zlist_first (self->mounts);
    while (mount) {
        if (mount_refresh (mount, self))
            activity = true;
        mount = (mount_t *) zlist_next (self->mounts);
    }
    if (activity)
        engine_broadcast_event (self, NULL, dispatch_event);

    return 0;
}

//  Allocate properties and structures for a new server instance.
//  Return 0 if OK, or -1 if there was an error.

static int
server_initialize (server_t *self)
{
    //  Construct properties here
    zsys_notice ("starting filemq service");
    self->mounts = zlist_new ();
    //  Register with the engine a function that will be called
    //  every second by the engine.
    engine_set_monitor (self, 1000, monitor_the_server);
    return 0;
}

//  Free properties and structures for a server instance

static void
server_terminate (server_t *self)
{
    //  Destroy properties here
    zsys_notice ("terminating filemq service");
    while (zlist_size (self->mounts)) {
        mount_t *mount = (mount_t *) zlist_pop (self->mounts);
        mount_destroy (&mount);
    }
    zlist_destroy (&self->mounts);
}

//  Process server API method, return reply message if any

static zmsg_t *
server_method (server_t *self, const char *method, zmsg_t *msg)
{
    if (streq (method, "PUBLISH")) {
        char *location = zmsg_popstr (msg);
        char *alias = zmsg_popstr (msg);
        mount_t *mount = mount_new (location, alias);
        zmsg_t *ret_msg = zmsg_new ();
        if (mount) {
            zlist_append (self->mounts, mount);
            zmsg_addstr (ret_msg, "SUCCESS");
        }
        else
            zmsg_addstr (ret_msg, "FAILURE");
        free (location);
        free (alias);
        return ret_msg;
    }

    return NULL;
}


//  Allocate properties and structures for a new client connection and
//  optionally engine_set_next_event (). Return 0 if OK, or -1 on error.

static int
client_initialize (client_t *self)
{
    //  Construct properties here
    self->patches = zlist_new ();
    return 0;
}

//  Free properties and structures for a client connection

static void
client_terminate (client_t *self)
{
    //  Destroy properties here
    mount_t *mount = (mount_t *) zlist_first (self->server->mounts);
    while (mount) {
        mount_sub_purge (mount, self);
        mount = (mount_t *) zlist_next (self->server->mounts);
    }
    while (zlist_size (self->patches)) {
        zdir_patch_t *patch = (zdir_patch_t *) zlist_pop (self->patches);
        zdir_patch_destroy (&patch);
    }
    zlist_destroy (&self->patches);
    zdir_patch_destroy (&self->patch);
    zfile_destroy (&self->file);
}


//  ---------------------------------------------------------------------------
//  store_client_subscription
//

static void
store_client_subscription (client_t *self)
{
    //  Find mount point with longest match to subscription
    const char *path = fmq_msg_path (self->message);

    mount_t *check = (mount_t *) zlist_first (self->server->mounts);
    mount_t *mount = check;
    while (check) {
        //  If check->alias is prefix of path and alias is
        //  longer than current mount then we have a new mount
        zsys_debug ("path=%s, check->alias=%s, mount->alias=%s",
            path, check->alias, mount->alias);
        if (strncmp (path, check->alias, strlen (check->alias)) == 0
        &&  strlen (check->alias) > strlen (mount->alias)) {
            zsys_debug ("prefix match, alias is longer");
            mount = check;
        }
        check = (mount_t *) zlist_next (self->server->mounts);
    }
    //  If subscription matches nothing, discard it
    if (mount) {
        zsys_debug ("new subscription being stored");
        mount_sub_store (mount, self, self->message);
    }
}


//  ---------------------------------------------------------------------------
//  store_client_credit
//

static void
store_client_credit (client_t *self)
{
    self->credit += fmq_msg_credit (self->message);
}


//  ---------------------------------------------------------------------------
//  get_next_patch_for_client
//

static void
get_next_patch_for_client (client_t *self)
{
    //  Get next patch for client if we're not doing one already
    if (self->patch == NULL)
        self->patch = (zdir_patch_t *) zlist_pop (self->patches);
    if (self->patch == NULL) {
        engine_set_next_event (self, finished_event);
        return;
    }
    //  Get virtual path from patch
    fmq_msg_set_filename (self->message, zdir_patch_vpath (self->patch));

    //  We can process a delete patch right away
    if (zdir_patch_op (self->patch) == patch_delete) {
        fmq_msg_set_sequence (self->message, self->sequence++);
        fmq_msg_set_operation (self->message, FMQ_MSG_FILE_DELETE);
        engine_set_next_event (self, send_delete_event);

        //  No reliability in this version, assume patch delivered safely
        zdir_patch_destroy (&self->patch);
    }
    else
    if (zdir_patch_op (self->patch) == patch_create) {
        //  Create patch refers to file, open that for input if needed
        if (self->file == NULL) {
            self->file = zfile_dup (zdir_patch_file (self->patch));
            if (zfile_input (self->file)) {
                //  File no longer available, skip it
                zdir_patch_destroy (&self->patch);
                zfile_destroy (&self->file);
                engine_set_next_event (self, next_patch_event);
                return;
            }
            self->offset = 0;
        }
        //  Get next chunk for file
        zchunk_t *chunk = zfile_read (self->file, CHUNK_SIZE, self->offset);
        assert (chunk);

        //  Check if we have the credit to send chunk
        if (zchunk_size (chunk) <= self->credit) {
            fmq_msg_set_sequence (self->message, self->sequence++);
            fmq_msg_set_operation (self->message, FMQ_MSG_FILE_CREATE);
            fmq_msg_set_offset (self->message, self->offset);

            self->offset += zchunk_size (chunk);
            self->credit -= zchunk_size (chunk);
            engine_set_next_event (self, send_chunk_event);

            //  Zero-sized chunk means end of file
            if (zchunk_size (chunk) == 0) {
                zfile_destroy (&self->file);
                zdir_patch_destroy (&self->patch);
            }
            fmq_msg_set_chunk (self->message, &chunk);
        }
        else {
            zchunk_destroy (&chunk);
            engine_set_next_event (self, no_credit_event);
        }
    }
}

//  ---------------------------------------------------------------------------
//  Selftest

void
fmq_server_test (bool verbose)
{
    printf (" * fmq_server: ");
    if (verbose)
        printf ("\n");
    
    //  @selftest
    zactor_t *server = zactor_new (fmq_server, "server");
    if (verbose)
        zstr_send (server, "VERBOSE");
    zstr_sendx (server, "BIND", "ipc://@/fmq_server", NULL);

    zsock_t *client = zsock_new (ZMQ_DEALER);
    assert (client);
    zsock_set_rcvtimeo (client, 2000);
    zsock_connect (client, "ipc://@/fmq_server");

    //  TODO: fill this out
    fmq_msg_t *message = fmq_msg_new ();
    fmq_msg_set_id (message, FMQ_MSG_OHAI);
    fmq_msg_send (message, client);
    fmq_msg_recv (message, client);
    assert (fmq_msg_id (message) == FMQ_MSG_OHAI_OK);
    fmq_msg_set_id (message, FMQ_MSG_KTHXBAI);
    fmq_msg_send (message, client);
    fmq_msg_destroy (&message);
    
    zsock_destroy (&client);
    zactor_destroy (&server);
    //  @end
    printf ("OK\n");
}
