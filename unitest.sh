#!/bin/zsh

MODULE=ftfs
MOUNT_POINT=./fs
MOUNT_IMG=./img

function u() # $1: testname, $2: test
{
    NAME="$1"
    shift
    $* >/dev/null 2>&1
    RET=$?
    if [[ $RET -eq 0 ]]; then
        printf "\e[32m[OK]\e[37m:%2d > \e[36m%s\e[37m\n" $RET $NAME
    else
        printf "\e[31m[KO]\e[37m:%2d > \e[36m%s\e[37m\n" $RET $NAME
    fi
    return $RET
}

function error()
{
    echo
    echo " === ERROR === "
    echo "\e[31mfatal error, test stopped ($1)\e[37m"
    python -c "import os; print '{}'.format(os.strerror($1))"
    exit
}

if [[ $EUID -ne 0 ]]; then
    echo "start the shell in root"
    return
fi

if [[ ! -e $MOUNT_IMG ]]; then
    echo "please create $MOUNT_IMG"
    return
fi

if [[ ! -d $MOUNT_POINT ]]; then
    mkdir $MOUNT_POINT
    echo "created $MOUNT_POINT directory"
fi

if mountpoint -q fs; then
    echo "$MOUNT_POINT already mounted"
    umount fs
    echo "  $MOUNT_POINT unmounted"
fi

if lsmod | grep -q $MODULE; then
    echo "$MODULE already loaded"
    rmmod $MODULE
    echo "  $MODULE unloaded"
fi

insmod $MODULE.ko && echo "$MODULE loaded" || return

echo
echo " === BEGINNING TEST === "
echo

u "mount"       mount -t fortytwofs -o loop $MOUNT_IMG $MOUNT_POINT || error $?
u "cd fs"       cd fs                                               || error $?
u "touch FILE"  touch FILE
u "test FILE"   test -e file
u "ls FILE"     ls -la FILE
u "mkdir DIR"   mkdir DIR                                           || error $?
u "test DIR"    test -d DIR
u "cd DIR"      cd DIR                                              || error $?
u "touch FILE"  touch FILE
u "test FILE"   test -e file

echo " === THE END === "
