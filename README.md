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

  J. Z. Kończak, P. T. Wojciechowski and R. Guerraoui, "Operation-Level Wait-Free Transactional Memory with
  Support for Irrevocable Operations," IEEE TPDS, vol. 28, no. 12, Dec 2017. doi: 10.1109/TPDS.2017.2734879

  J. Z. Kończak, P. T. Wojciechowski and R. Guerraoui, "Ensuring Irrevocability in Wait-free
  Transactional Memory", TRANSACT 2016
    
```
