mboxstats: create top-10 lists from messages in mbox-format
===========================================================

See home page at http://www.vanheusden.com/mboxstats/.

mboxstats creates top-10 lists from an mbox-format archive, for
example:

    Top writes
    Top receivers
    Top subjects
    Top cc'ers
    Top top-level-domain
    Top timezones
    Top organisations
    Top useragents (mailprograms)
    Top month/day-of-month/day-of-week/hour
    Average number of lines per message
    All kinds of per-user statistics
    And much more! 

As of 9 December 2013 / version 3.0, it didn't have its own README, so
I made this README based on http://www.vanheusden.com/mboxstats/.  As
far as I can tell, the author does not keep the mboxstats source code
in public version control repository, so I made this import from the
3.0 tarball at http://www.vanheusden.com/mboxstats/mboxstats-3.0.tgz.

On the web site, the author asks people to report usage of mboxstats:
http://www.vanheusden.com/mboxstats/feedbackform.php?subject=mboxstats

Installation
------------

The usual:

    $ make
    $ make install

You'll get a lot of warnings on the `make` step; just ignore them.

Cute trick: if you want to let the author know you're using mboxstats,
you can apparently run

    $ make thanks

Usage
-----

After building, run

    $ ./mboxstats -h

to see usage instructions.

Feedback
--------

I suggest using the author's feedback from, rather than bug reports
attached to this repository, to send feedback about mboxstats:

  http://www.vanheusden.com/mboxstats/feedbackform.php?subject=mboxstats

-Karl Fogel (@kfogel)
 [not the author of mboxstats, merely the repository assembler]
