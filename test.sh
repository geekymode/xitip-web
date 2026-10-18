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

# non-Shannon-type: true, by the known inequalities, and said so
expect TRUE  '2 I(C;D) <= I(A;B) + I(A;C,D) + 3 I(C;D|A) + I(C;D|B)'
expect TRUE  '2 I(A;B) <= 5 I(A;B|C) + 3 I(A;C|B) + I(B;C|A) + 2 I(A;B|D) + 2 I(C;D)'
expect TRUE  'I(C;D) <= I(C;D|A) + I(C;D|B)' 'I(A;B) = 0' 'I(A;B|C) = 0'
expect TRUE  'I(X;Y) <= 2 I(X;Y|Z) + I(X;Z|Y) + I(Y;Z|X) + I(X;Y|W) + I(Z;W) + H(V)'
expect FALSE 'I(C;D) <= I(C;D|A) + I(C;D|B)'
named=$("$BIN" --prove '2 I(C;D) <= I(A;B) + I(A;C,D) + 3 I(C;D|A) + I(C;D|B)')
case "$named" in
    *"non-Shannon-type"*"Zhang–Yeung inequality (1998)"*) echo "ok   names Zhang–Yeung";;
    *) echo "FAIL: Zhang–Yeung not named"; fail=1;;
esac
case "$("$BIN" --prove --shannon '2 I(C;D) <= I(A;B) + I(A;C,D) + 3 I(C;D|A) + I(C;D|B)')" in
    *"is FALSE."*) echo "ok   --shannon leaves it FALSE";;
    *) echo "FAIL: --shannon"; fail=1;;
esac

# the default mode must keep counting variables
count=$("$BIN" 'I(W;Z) <= I(X;Y)' 'W/X/Y/Z')
[ "$count" = 4 ] && echo "ok   count 4" || { echo "FAIL count: $count"; fail=1; }

[ $fail -eq 0 ] && echo "\nall good" || echo "\nsomething failed"
exit $fail
