OpenTTD README
Last updated:    2005-09-09
Release version: 0.4.1
------------------------------------------------------------------------


Table of Contents:
------------------
1.0) About
2.0) Contacting
 * 2.1 Reporting Bugs
3.0) Supported Platforms
4.0) Running OpenTTD
5.0) OpenTTD features
6.0) Configuration File
7.0) Compiling
8.0) Translating
 * 8.1 Guidelines
 * 8.2 Translation
 * 8.3 Previewing
9.0) Troubleshooting
X.X) Credits


1.0) About:
---- ------
OpenTTD is a clone of Transport Tycoon Deluxe, a popular game originally
written by Chris Sawyer.  It attempts to mimic the original game as closely
as possible while extending it with new features.

OpenTTD is licensed under the GNU General Public License version 2.0. For
more information, see the file 'COPYING'.

2.0) Contacting:
---- ----------
The easiest way to contact the OpenTTD team is by submitting bug reports or
posting comments in our forums. You can also chat with us on IRC (#openttd
on irc.freenode.net).

The OpenTTD homepage is http://www.openttd.org/.

You can also find the OpenTTD forums at
http://www.tt-forums.net/index.php?c=20


2.1) Reporting Bugs:
---- ---------------
To report a bug, please create a SourceForge account and follow the bugs
link from our homepage. Please make sure the bug is reproducible and
still occurs in the latest daily build or the current SVN version. Also
please look through the existing bug reports briefly to see whether the bug
is not already known.

The SourceForge project page URL is: http://sourceforge.net/projects/openttd/
Click on "Bugs" to see the bug tracker.

Please include the following information in your bug report:
        - OpenTTD version (PLEASE test the latest SVN/daily build)
        - Bug details, including instructions how to reproduce it
        - Platform and compiler (Win32, Linux, FreeBSD, ...)
        - Attach a saved game or a screenshot if possible
        - If this bug only occurred recently please note the last
          version without the bug and the first version including
          the bug. That way we can fix it quicker by looking at the
          changes made.


3.0) Supported Platforms:
---- --------------------
OpenTTD has been ported to several platforms and operating systems. It shouldn't
be very difficult to port it to a new platform. The currently working platforms
are:

	BeOS     - SDL
	FreeBSD  - SDL
	Linux    - SDL
	Mac OS X - SDL
	MorphOS  - SDL
	OS/2     - SDL
	Windows  - Win32 GDI (faster) or SDL


4.0) Running OpenTTD:
---- ----------------

Before you run OpenTTD, you need to put the game's datafiles into the data/
subdirectory. You need the following files from the original version
of TTD as OpenTTD makes use of the original TTD artwork.

List of the required files:
sample.cat
trg1r.grf
trgcr.grf
trghr.grf
trgir.grf
trgtr.grf

(Alternatively you can use the TTD GRF files from the DOS version: TRG1.GRF,
TRGC.GRF, TRGH.GRF, TRGI.GRF, TRGT.GRF. A few minor graphical glitches with
the DOS graphics remain. E.g. the autorail button in the rail toolbar doesn't
look as nice as with the Windows graphics.)

If you want music you need to copy the gm/ folder from Windows TTD into your
OpenTTD folder, not your data folder.

You can change the data path (which contains savegames as well) in
Makefile.config by setting DATA_DIR_PREFIX and USE_HOMEDIR.


5.0) OpenTTD features:
---- -----------------

OpenTTD has a lot of features going beyond the original TTD emulation.
Unfortunately, there is currently no comprehensive list of features, but there
is a basic features list on the web, and some optional features can be
controlled through the Configure Patches dialog. We also implement some
features known from TTDPatch (http://www.ttdpatch.net/).

Several important non-standard controls:

* Use Ctrl to place semaphore signals
* Ctrl-D toggles double mode in the Windows version
* Ingame console. More information at
  http://wiki.openttd.org/index.php/OpenTTDDevBlackBook


6.0) Configuration File:
---- -------------------
The configuration file for OpenTTD (openttd.cfg) is in a simple Windows-like
.INI format. It's mostly undocumented. Almost all settings can be changed
ingame by using the 'Configure Patches' window.


7.0) Compiling:
---- ----------
Windows:
  You need Microsoft Visual Studio 6 or .NET. Open the project file
  and it should build automatically. In case you want to build with SDL support
  you need to add WITH_SDL to the project settings.
  PNG (WITH_PNG) and ZLIB (WITH_ZLIB) support is enabled by default. For these
  to work you need their development files. For best results, download the
  openttd-useful.zip file from SourceForge under the Files tab. Put the header
  files into your compiler's include/ directory and the library (.lib) files
  into the lib/ directory.
  For more help with VS6 see docs/Readme_Windows_MSVC6.0.txt.

  You can also build it using the Makefile with MSYS/MinGW or Cygwin/MinGW.
  Please read the Makefile for more information.

Unix:
  OpenTTD can be built with GNU "make". On non-GNU systems it's called "gmake".
  Note that you need SDL-devel 1.2.5 (or higher) to compile OpenTTD.
  (see also docs/Readme_Mandrake_Linux.txt)

MacOS:
  Use "make".

BeOS:
  Use "make".

FreeBSD:
  You need the port devel/sdl12 for a non-dedicated build.
  graphics/png is optional for screenshots in the PNG format.
  Use "gmake".

MorphOS:
  Use "make". Note that you need the MorphOS SDK and the
  powersdl.library SDK.

OS/2:
  Open Watcom C/C++ 1.3 or later is required to build the OS/2 version. See the
  docs/Readme_OS2.txt file for more information.


8.0) Translating:
---- -------------------
See http://www.openttd.org/translating.php for up-to-date information.

The use of the online Translator service, located at
http://translator.openttd.org/, is highly encouraged. For a username/password
combo you should contact the development team, either by mail, IRC or the
forums. The system is straightforward to use, and if you have any problems,
read the online help located there.

If for some reason the website is down for a longer period of time, the
information below might be of help.

8.1) Guidelines:
---- -------------------
Here are some translation guidelines which you should follow closely.

    * Please contact the development team before begining the translation
      process! This avoids double work, as someone else may have already
      started translating to the same language.
    * Translators must use the charater set ISO 8859-15.
      Otherwise, some characters will not display correctly in the game.
    * Currently it is not possible to translate into character sets other than
      Latin.

8.2) Translation:
---- -------------------
So, now that you've notified the development team about your intention to
translate (You did, right? Of course you did.) you can pick up english.txt
(found in the SVN repository under /lang) and translate.

You must change the first two lines of the file appropriately:

##name English-Name-Of-Language
##ownname Native-Name-Of-Language

Note: Do not alter the following parts of the file:

    * String identifiers (the first word on each line)
    * Parts of the strings which are in curly braces (such as {STRING})
    * Lines beginning with ## (such as ##id), other than the first two lines of
      the file

8.3) Previewing:
---- -------------------
In order to view the translation in the game, you need to compile your language
file with the strgen utility, which is now bundled with the game.

strgen is a command-line utility. It takes the language filename as parameter.
Example:

strgen lang/german.txt

This results in compiling german.txt and produces another file named german.lng.
Any missing strings are replaced with the English strings. Note that it looks
for english.txt in the lang subdirectory, which is where your language file
should also be.

That's all! You should now be able to select the language in the game options.

9.0) Troubleshooting
---- ---------------

To see all startup options available to you, start OpenTTD with the
"./openttd -h" option. This might help you tweak some of the settings.

If the game is acting strange and you feel adventorous you can try the
"-d [[<name>]=[<level>]" flag, where the higher levels will give you more
debugging output. The "name" variable can help you to display only some type of
debugging messages. This is mostly undocumented so best is to look in the
source code file debug.c for the various debugging types. For more information
look at http://wiki.openttd.org/index.php/Command_line.

The most frequent problem is missing data files. Don't forget to put all GRF
files from TTD into your data/ folder including sample.cat!

Under Windows 98 and lower it is impossible to use a dedicated server; it will
fail to start. Perhaps this is for the better because those OS's are not known
for their stability.

X.X) Credits:
---- --------
The OpenTTD team (in alphabetical order):
  Bjarni Corfitzen (Bjarni)      - Mac OS X port, coder
  Victor Fischer (Celestar)      - Programming everywhere you need him to
  Tam�s Farag� (Darkvater)       - Lead programmer
  Kerekes Miham (MiHaMiX)        - Maintainer of translator service, and host of nightlies
  Owen Rudge (orudge)            - Contributor, forum host, master server host, OS/2 port
  Peter Nelson (peter1138)       - NewGRF support wizard, OpenTTD addict
  Christoph Mallon (Tron)        - Programmer, code correctness police
  Patric Stout (TrueLight)       - Programmer, network guru, SVN-repository and website host

Retired Developers:
  Dominik Scherer (dominik81)    - Lead programmer, GUI expert (0.3.0 - 0.3.6)
  Ludvig Strigeus (ludde)        - OpenTTD author, main coder (0.1 - 0.3.3)
  Serge Paquet (vurlix)          - Assistant project manager, coder (0.1 - 0.3.3)

Thanks to:
  Josef Drexler - For his great work on TTDPatch.
  Marcin Grzegorczyk - For his TTDPatch work and documentation of TTD internals and graphics (signals and track foundations)
  Matthijs Kooijman (blathijs) - For his many patches, suggestions and major work on maprewrite
  Petr Baudis (pasky) - Many patches, newgrf support, etc.
  Simon Sasburg (HackyKid) - For the many bugfixes he has blessed us with (and future PBS)
  Stefan Mei�ner (sign_de) - For his work on the console
  Mike Ragsdale - OpenTTD installer
  Cian Duffy (MYOB) - BeOS port / manual writing
  Christian Rosentreter (tokaiz) - MorphOS / AmigaOS port
  Michael Blunck - For revolutionizing TTD with awesome graphics
  George - Canal graphics
  All Translators - For their support to make OpenTTD a truly international game
  Bug Reporters - Thanks for all bug reports
  Chris Sawyer - For an amazing game!
