#!/usr/bin/bash

# run with source or '. scripts/init_debug_env.sh' to allow
# modification of the active environment's variables
# usage: . scripts/init_debug_env.sh [--lsan <path>] [--valgrind <path>]

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "WARNING: Script ran without it being sourced, environment variable changes won't be visible."
    echo "Source the script to fix this: . scripts/init_debug_env.sh [options]"
    echo ""
fi

LSAN_SUPP_PATH=""
VALGRIND_SUPP_PATH=""

while [ $# -gt 0 ]; do
    case $1 in
        -h|--help)
            echo "Usage: . scripts/init_debug_env.sh [--lsan <path>] [--valgrind <path>]"
            return 1
            ;;
        --valgrind)
            if [ "$2" ] && [[ $2 != "-"* ]]; then
                VALGRIND_SUPP_PATH=$(realpath "$2")
                shift
            else
                echo "--valgrind has to be followed by a <path> argument"
                return 1
            fi
            ;;
        --lsan)
            if [ "$2" ] && [[ $2 != "-"* ]]; then
                LSAN_SUPP_PATH=$(realpath "$2")
                shift
            else
                echo "--lsan has to be followed by a <path> argument"
                return 1
            fi
            ;;
        *)
            echo "Unknown option: $1"
            return 1
            ;;
    esac

    shift
done

SCRIPT_DIR=$(cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd)

if [ -z "$LSAN_SUPP_PATH" ]; then
    LSAN_SUPP_PATH="$SCRIPT_DIR/../misc/lsan.supp"
fi

if [ -z "$VALGRIND_SUPP_PATH" ]; then
    VALGRIND_SUPP_PATH="$SCRIPT_DIR/../misc/valgrind.supp"
fi

if [ ! -f "$LSAN_SUPP_PATH" ]; then
    echo "LSan suppression file not found at $LSAN_SUPP_PATH"
    echo "If you moved this script outside the scripts/ directory, or you want to use a custom suppression file, you may specify the file path using --lsan <PATH>"
    return 1
fi

if [ ! -f "$VALGRIND_SUPP_PATH" ]; then
    echo "Valgrind suppression file not found at $VALGRIND_SUPP_PATH"
    echo "If you moved this script outside the scripts/ directory, or you want to use a custom suppression file, you may specify the file path using --valgrind <PATH>"
    return 1
fi

export LSAN_OPTIONS="suppressions=$LSAN_SUPP_PATH"

if [[ " $VALGRIND_OPTS " != *" --suppressions=$VALGRIND_SUPP_PATH "* ]]; then
    export VALGRIND_OPTS="$VALGRIND_OPTS --suppressions=$VALGRIND_SUPP_PATH"
else
    echo "Valgrind suppression file already set in VALGRIND_OPTS"
fi

echo "Active LSan suppression file path: $LSAN_SUPP_PATH"
echo "Active Valgrind suppression file path: $VALGRIND_SUPP_PATH"
