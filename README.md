```
Operation-Level Wait-Free Transactional Memory with Support for Irrevocable Operations
======================================================================================

This repository contains implementation of a TM algorithm that guarantees:
  • no forced waiting on any transactional operation,
  • support for irrevocability,
  • strong progressiveness,
  • opacity.


Files
=====

├── README.md              - this file
├── CMakeLists.txt         \
└── src                     |
    ├── tmapi.h             |
    ├── tmapi.cpp           |  TM implementation
    ├── variable.h          |
    ├── variable.cpp        |
    ├── transaction.h       |
    ├── transaction.cpp    /
    │
    ├── speed.cpp           \  microbenchmarks 
    └── microbenchmark.cpp  /

microbenchmarks depend on boost


LICENSE
=======

Contents of this repository is licensed under the GNU General Public License, version 3.


If you wish to use the TM for research purpose, we kindly ask you to cite the following paper:

  Ensuring Irrevocability in Wait-free Transactional Memory, Jan Kończak, Paweł T. Wojciechowski
  and Rachid Guerraoui, TRANSACT 2016
    
```
