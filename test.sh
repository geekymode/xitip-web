#!/bin/sh
# Verdicts, and that every true statement comes with a proof.
set -u
BIN=${1:-./oXitipLen}
fail=0

expect() {                       # expect TRUE|FALSE expr [constraints...]
    want=$1; shift
    out=$("$BIN" --proof "$@" 2>&1)
    case "$out" in
        *"is $want."*) ;;
        *) echo "FAIL ($want): $*"; fail=1; return;;
    esac
    if [ "$want" = TRUE ]; then
        case "$out" in
            *"no proof it could stand by"*)
                echo "FAIL (no proof): $*"; fail=1; return;;
            *"Proof of"*) ;;
            *) echo "FAIL (proof missing): $*"; fail=1; return;;
        esac
    fi
    echo "ok   $want  $*"
}

expect TRUE  'H(X,Y,Z) <= H(X,Y) + H(Z)'
expect TRUE  '2 H(X,Y,Z) <= H(X,Y) + H(Y,Z) + H(X,Z)'
expect TRUE  'I(X;Y) <= H(X)'
expect TRUE  'H(X,Y) >= H(X)'
expect TRUE  'H(X1,X2,X3,X4) <= H(X1)+H(X2)+H(X3)+H(X4)'
expect TRUE  'I(W;Z) <= I(X;Y)' 'W/X/Y/Z'
expect TRUE  'H(X) <= H(Y)' 'X:Y'
expect TRUE  'H(X) >= 1' 'H(X) >= 2'
expect TRUE  'I(X;Y) = H(X) + H(Y) - H(X,Y)'
expect FALSE 'I(X;Y|Z) <= I(X;Y)'
expect FALSE 'I(X;Y;Z) >= 0'
expect FALSE 'H(X) <= H(Y)'
expect FALSE 'I(A;B) <= I(A;B|C) + I(A;B|D) + I(C;D)'

# the default mode must keep counting variables
count=$("$BIN" 'I(W;Z) <= I(X;Y)' 'W/X/Y/Z')
[ "$count" = 4 ] && echo "ok   count 4" || { echo "FAIL count: $count"; fail=1; }

[ $fail -eq 0 ] && echo "\nall good" || echo "\nsomething failed"
exit $fail
