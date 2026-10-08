oXitipLen
=========

An Information Theoretic Inequality Prover (C++/CLI), derived from Citip.
It counts the distinct random variables in an expression, proves
Shannon-type inequalities, and prints the proof.

In a browser
------------

The prover also builds to WebAssembly, so it runs in a browser with no
server and nothing installed: https://geekymode.github.io/xitip-web/

Everything in ``docs/`` is that site. Nothing typed into it leaves the
machine. See ``web/README.md`` for how it is built, tested and served, and
for a small HTTP server that runs the native binary instead.

Usage
-----

With no option it prints the number of distinct random variables, which is
what it has always done::

    $ ./oXitipLen 'I(W;Z) <= I(X;Y)' 'W/X/Y/Z'
    4

``--prove`` decides the expression::

    $ ./oXitipLen --prove 'I(X;Y|Z) <= I(X;Y)'
    The information expression is FALSE.

``--proof`` (or ``--steps``) also prints why::

    $ ./oXitipLen --proof 'H(X,Y,Z) <= H(X,Y) + H(Z)'
    Proof of  E >= 0  where  E = H(X,Y) + H(Z) - H(X,Y,Z)  =  I(X,Y;Z)

      E  =  H(X,Y) + H(Z) - H(X,Y,Z)
         =  I(X;Z|Y)  +  [ I(Y;Z) ]
         =  I(X;Z|Y)  +  I(Y;Z)

      where every term is non-negative:
        I(X;Z|Y) = -H(Y) + H(X,Y) + H(Y,Z) - H(X,Y,Z)
        I(Y;Z) = H(Y) + H(Z) - H(Y,Z)

    The information expression is TRUE.

The first expression is the statement; any further ones are constraints.
With no expression, or with ``-`` last, they are read from standard input,
one per line.

How the proof is found
----------------------

The expression is minimised over the cone cut out by the elemental
inequalities and the constraints. If the minimum is non-negative the
expression holds, and the multipliers that write it as a non-negative
combination of those inequalities are the **dual solution** of that same
linear program: the row duals weight the inequalities, the reduced costs
weight the ``H(S) >= 0`` column bounds, and what is left over is a
non-negative constant.

An equality constraint may be used either way round, which is what a
negative multiplier on it means; such a term is marked *(reversed)*.

The simplex works in floating point, so the combination is recomputed and
compared against the expression before it is printed. If it does not add
up, the verdict still stands but no proof is shown, rather than a proof
that cannot be trusted.

A proof of the same shape is produced by Xitip.jl, the Julia
implementation, and the two agree on the test cases in ``test.sh``.

Citip is derived from Xitip_ which is based on ITIP_. Xitip was made
by *Rethnakaran Pulikkoonattu*, *Etienne Perron* and *Suhas Diggavi*. ITIP
was created by *Raymond W. Yeung* and *Ying-On Yan*.

.. _Xitip: http://xitip.epfl.ch/
.. _ITIP: http://user-www.ie.cuhk.edu.hk/~ITIP/


Why another fork?
-----------------

Originally, I just wanted to replace the GTK frontend by a more convenient
CLI interface, because I found the GUI annoying, inconvenient to work with
and pointless for this particular application. Thinking further, CLI based
applications are also better for automatization and require fewer run-time
dependencies.

-rant

Another reason was to provide a public platform for possible continued
development of the application. I was unable to get in contact with any of
the original authors of Xitip (emails are dead), and there is (AFAIK) no
public VCS. **If you have any ideas and/or patches to contribute, don't be
shy!**, just open an issue or send me a pull-request.

I didn't plan to do much apart from that. By now all the source files have
been completely rewritten. The main differences to Xitip are:

- replace the GTK frontend by a simple CLI frontend
- ported to the free software GLPK_ library for linear programming
- extend the accepted grammar in a few places
- more maintainable code base
- compilation requires a C++11 compliant compiler and recent versions of
  flex and bison

For a more detailed list of changes see CHANGES.rst_ and ultimately the
commit history.

.. _GLPK: https://www.gnu.org/software/glpk/
.. _CHANGES.rst: https://github.com/coldfix/Citip/blob/master/CHANGES.rst


Build
-----

Requirements: a C++11 compiler, GLPK_, flex and bison >= 3.0.

On Debian/Ubuntu:

.. code-block:: bash

    sudo apt-get install libglpk-dev flex bison
    make

On macOS the system bison (2.3) is too old, and Homebrew installs GLPK and
bison outside the default search paths. The Makefile picks up Homebrew's
GLPK and bison automatically:

.. code-block:: bash

    brew install glpk bison
    make

Without Homebrew, pass the locations explicitly, e.g.
``make BISON=/path/to/bison GLPK_PREFIX=/path/to/glpk``.

Alternatively, build with cmake:

.. code-block:: bash

    brew install glpk bison cmake
    cmake -S . -B cmake-build \
          -DCMAKE_PREFIX_PATH="$(brew --prefix)" \
          -DBISON_EXECUTABLE="$(brew --prefix bison)/bin/bison"
    cmake --build cmake-build

Both builds produce an executable named ``oXitipLen`` (in the source
directory for ``make``, in ``cmake-build/`` for cmake). The generated
parser and scanner sources depend on the installed flex/bison versions, so
don't copy the ``build/`` directory between machines; run ``make clean``
instead.

In a browser
------------

The prover also builds to WebAssembly, so it runs in a browser with no
server and nothing installed: https://geekymode.github.io/xitip-web/

Everything in ``docs/`` is that site. Nothing typed into it leaves the
machine. See ``web/README.md`` for how it is built, tested and served, and
for a small HTTP server that runs the native binary instead.

Usage
-----

The expressions can be passed either as command line arguments or (if no
command line arguments are provided or the last one is -) via STDIN, one
per line. The first expression is the inequality, all others are
constraints; the variables of all of them are counted.

The number of distinct random variables is printed to STDOUT:

.. code-block:: bash

    $ ./oXitipLen 'I(X;Y|Z) <= I(X;Y)'
    3

    (exit code = 0)


    $ ./oXitipLen 'I(X;Y|Z) <= I(X;Y)' 'H(W) = 0'
    4

    (exit code = 0)


    $ ./oXitipLen 'I(X;;Y|Z) <= I(X;Y)'
    ERROR: syntax error, unexpected ';', expecting NAME
    in row 1 col 5:

        I(X;;Y|Z) <= I(X;Y)
            ^

    (exit code = 2)

Errors are printed to STDERR. The exit code only signals success or
failure, never the count::

    0 - Success, count printed to STDOUT
    2 - Error (e.g. syntax error, too many variables)
    3 - Unknown error


License
-------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
