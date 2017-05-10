#!/bin/zsh

MODULE=ftfs
MOUNT_POINT=./fs
MOUNT_IMG=./img

function err()
{
    echo "\e[38;5;196m!!! $@\e[37m"
}
function log()
{
    echo "\e[38;5;208m### $@\e[37m"
}

function u() # $1: testname, $2: test
{
    NAME="$1"
    shift
    printf "> \e[36m%s\e[37m\n" "$NAME"
    $* >/dev/null 2>&1
    RET=$?
    if [[ $RET -eq 0 ]]; then
        printf "\e[32m[OK]\e[37m:%2d\n" $RET
    else
        printf "\e[31m[KO]\e[37m:%2d\n" $RET
    fi
    echo
    return $RET
}

function error()
{
    echo
    err "ERROR"
    err "\e[31mfatal error, test stopped ($1)\e[37m"
    python -c "import os; print '{}'.format(os.strerror($1))"
    exit
}

if [[ $EUID -ne 0 ]]; then
    err "start the shell in root"
    return
fi

if [[ ! -e $MOUNT_IMG ]]; then
    err "please create $MOUNT_IMG"
    return
fi

if [[ ! -d $MOUNT_POINT ]]; then
    mkdir $MOUNT_POINT
    log "created $MOUNT_POINT directory"
fi

if mountpoint -q $MOUNT_POINT; then
    log "$MOUNT_POINT already mounted"
    umount $MOUNT_POINT
    log "  $MOUNT_POINT unmounted"
fi

if lsmod | grep -q $MODULE; then
    log "$MODULE already loaded"
    rmmod $MODULE
    log "  $MODULE unloaded"
fi

insmod $MODULE.ko && log "$MODULE loaded" || return


function do_sh()
{
    log starting shell
    bash
}

function do_test()
{
    log
    log "=== BEGINNING TEST === "
    log

    u "cd fs"          cd fs                                               || error $?

    u "create"         touch FILE
    u "stat file"      test -e FILE

    u "mkdir"          mkdir DIR                                           || error $?
    u "test dir"       test -d DIR
    u "cd"             cd DIR                                              || error $?

    u "create subfile" touch FILE
    u "stat subfile"   test -e FILE
    u "rm subfile"     rm FILE

    u "cd"             cd ..
    u "rmdir"          rmdir DIR

    u "write"          echo coucou > FILE
    u "read"           cat FILE
    u "check write"    grep -q coucou FILE

    log "=== THE END === "
}


log "mounting $MOUNT_IMG on $MOUNT_POINT"
mount -t fortytwofs -o loop $MOUNT_IMG $MOUNT_POINT

pushd $PWD

for arg in $@; do
    case $arg in
        "sh")
            do_sh
            ;;
        "test")
            do_test
            ;;
        *)
            err unknown arg $arg
            ;;
    esac
done

popd

log "unmounting $MOUNT_POINT"
umount $MOUNT_POINT
log "unloading $MODULE"
rmmod $MODULE
