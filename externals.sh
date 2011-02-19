#!/bin/bash

if [ -d m4 ]; then
    svn up m4
else
    svn checkout http://svn.xiph.org/icecast/trunk/m4
fi

if [ -d src/avl ]; then
    svn up src/avl
else
    svn checkout http://svn.xiph.org/icecast/trunk/avl src/avl
fi

if [ -d src/httpp ]; then
    svn up src/httpp
else
    svn checkout http://svn.xiph.org/icecast/trunk/httpp src/httpp
fi

if [ -d src/log ]; then
    svn up src/log
else
    svn checkout http://svn.xiph.org/icecast/trunk/log src/log
fi

if [ -d src/net ]; then
    svn up src/net
else
    svn checkout http://svn.xiph.org/icecast/trunk/net src/net
fi

if [ -d src/thread ]; then
    svn up src/thread
else
    svn checkout http://svn.xiph.org/icecast/trunk/thread src/thread
fi

if [ -d src/timing ]; then
    svn up src/timing
else
    svn checkout http://svn.xiph.org/icecast/trunk/timing src/timing
fi
