# FileMQ implementation in C

This project is an updated attempt at FileMQ which is a publish-subscribe file service based on 0MQ. The original project resides at http://github.com/zeromq/filemq. The specifications for the FileMQ protocol reside at http://rfc.zeromq.org/spec:35.

The following is an explanation from the original project...

## Why FileMQ?

"Request-reply is just a vulgar subclass of publish-subscribe" -- me, in "Software Architecture over 0MQ".

It's the API my 5-year old son can use. It runs on every machine there is. It carries binary objects of any size and format. It plugs into almost every application in existence. Yes, it's your file system.

So this is FileMQ. It's a "chunked, flow-controlled, restartable, cancellable, async, multicast file transfer ØMQ protocol", server and client. The protocol spec is at http://rfc.zeromq.org/spec:19. It's a rough cut, good enough to prove or disprove the concept.

What problems does FileMQ solve? Well, two main things. First, it creates a stupidly simple pub-sub messaging system that literally anyone, and any application can use. You know the 0MQ pub-sub model, where a publisher distributes files to a set of subscribers. The radio broadcast model, where you join at any time, get stuff, then leave when you're bored. That's FileMQ. Second, it wraps this up in a proper server and client that runs in the background. You may get something similar to the DropBox experience but there is no attempt, yet, at full synchronization, and certainly not in both directions.

End explanation

## Installation for general use

This code needs the latest versions of libsodium, libzmq, and CZMQ. To build:

    git clone git://github.com/jedisct1/libsodium.git
    cd libsodium
    ./autogen.sh
    ./configure
    make check
    sudo make install
    sudo ldconfig
    cd ..

    git clone git://github.com/zeromq/libzmq.git
    cd libzmq
    ./autogen.sh
    ./configure
    make check
    sudo make install
    sudo ldconfig
    cd ..

    git clone git://github.com/zeromq/czmq.git
    cd czmq
    ./autogen.sh
    ./configure
    make check
    sudo make install
    sudo ldconfig
    cd ..

    git clone git://github.com/danriegsecker/filemq2.git
    cd filemq2
    ./autogen.sh
    ./configure
    make check
    sudo make install
    sudo ldconfig
    cd ..

## Installation for modification

Follow all the steps for "Installation for general use" and additionally do the following steps.

    git clone git://github.com/zeromq/zproject.git
    cd zproject
    ./autogen.sh
    ./configure
    make check
    sudo make install
    cd ..

    git clone git://github.com/zeromq/zproto.git
    cd zproto
    ./autogen.sh
    ./configure
    make check
    sudo make install
    cd ..
