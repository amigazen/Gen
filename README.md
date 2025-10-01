# _Gen_ 

This is _Gen_, a set of build and package management tools for Amiga, of amigazen project ToolKit.

## [amigazen project](http://www.amigazen.com)

*A web, suddenly*

*Forty years meditation*

*Minds awaken, free*

**amigazen project** uses modern software development tools and methods to update and rerelease classic Amiga open source software. Releases include a new AWeb, the _unsui_ platform, and the ToolKit project - a universal SDK for Amiga.

Key to the amigazen project approach is ensuring every project can be built with the same common set of development tools and configurations, so the ToolKit was created to provide a standard configuration for Amiga development. All *amigazen project* releases will be guaranteed to build against the ToolKit standard so that anyone can download and begin contributing straightaway without having to tailor the toolchain for their own setup.

Our philosophy is based on openness:

*Open* to anyone and everyone	- *Open* source and free for all	- *Open* your mind and create!

PRs for all amigazen projects are gratefully received at [GitHub](https://github.com/amigazen/). While the amigazen project focus now is on classic 68k software, it is intended that all amigazen project releases can be ported to other Amiga and Amiga-like systems including AROS and MorphOS where feasible.

## About _Gen_

_Gen_ is a set of build and package management tools for Amiga software development, designed to be used with the amigazen project ToolKit. It consists of several tools to help with creating and managing SDK components and software releases, including:

- **GenIn** - a command line tool to create Amiga "DiskObjects" AKA .info files, Amiga's native icon and metadata file format.
- **GenMaki** - a command line tool to create and convert makefiles in different Amiga native formats.
- **GenDo** - a command line tool to generate Autodoc and AmigaGuide API documentation from marked up source code
- **GenKei** - a command line tool to generate project templates (Coming Soon).
- **GenGen** - a command line tool to generate manifest files for Gen packages (Coming Later).
- **KaiZen** - a command line tool to automatically apply updates to ToolKit (Coming Much Later).

In Japanese, the syllable _gen_ means current, actual or realized, but also more poetically can be interpreted as 'manifest' in the sense of manifesting something into existence. It's a happy coincidence that the word root gen- in european languages means 'create'.

### GenIn (現印)

GenIn is the first tool released as part of the _Gen_ project - a command line tool for automatically manifesting or _gen_-erating _in_-fo files during a build process or indeed any script. 

The name GenIn is inspired, like the Gen project itself, by the Japanese syllables *gen* and *in* (現印) meaning together *genin* or 'manifested stamp, seal or symbol'. 

### GenMaki (現巻)

GenMaki is a dependency analyzer that can create or check makefiles in the different formats found on Amiga, including SAS/C smakefiles, GNU makefiles, Lattice lmkfiles and DICE dmakefiles as well as convert between them.

The name GenMaki is inspired by the Japanese syllables *gen* and *maki* (現巻) meaning together *genmaki* or 'manifested scroll'

### GenDo (現道)

GenDo is an AutoDoc generator tool that can extract Autodoc markup from source code into Amiga standard Autodoc 'doc' files. Unlike the original Autodoc tool, included with the NDK, GenDo supports wildcards allowing a single autodoc to be assembled from multiple project files, and also supports creation of an AmigaGuide version of the Autodoc at the same time, removing the need to run a separate tool.

The name GenDo is inspired by the Japanese syllables *gen* and *do* (道) meaning together *gendo* or 'manifested path' where _do_ is more commonly interpreted as skill or technique.

### GenKei (原型)

GenKei creates new code skeletons for a variety of different kinds of Amiga software development Projects according to a set of templates, implementing best practices for startup, API management, directory structure etc.

The name GenKei is also inspired, like the Gen project itself, by the Japanese syllables *gen* and *kei* (原型) meaning together *genkei* or 'prototype or pattern'. 

### GenGen (原原)

GenGen manifests manifests.

(More details coming later)

### KaiZen

(More details coming much later)

## Frequently Asked Questions

### What is the origin of the name Gen and GenIn?

The Gen system is named for both the Japanese word 'Gen' meaning actual, current, up-to-date or made-manifest, and by happy coincidence also resonates in european languages as the root of words like generate, meaning 'create'. 

Since makeinfo is a poorly named but well known Unix command from the texinfo package any name like MakeInfo or GenInfo is likely to cause confusion. The syllable *in* (印) is used to mean an official seal or stamp, perfect for an _i_-co-_n_ or _in_-fo file!

### Who are/what is amigazen project?

To learn more about amigazen project and its aims, see AMIGAZEN.md or the Amiga-compatible website at http://www.amigazen.com/

## Contact 

- At GitHub https://github.com/amigazen/Gen/ 
- on the web at http://www.amigazen.com/ToolKit/ (Amiga browser compatible)
- or email ToolKit@amigazen.com

## Acknowledgements

*Amiga* is a trademark of **Amiga Inc**. 
