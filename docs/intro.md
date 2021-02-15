# Introduction

Cosmo is a lightweight embeddable scripting language written in C99. Cosmo has comparable syntax to Lua 5.1, so if you are familiar with that syntax, learning Cosmo should be trivial. Cosmo has eccentric support for object-oriented programming, procedural programming, and functional programming. To see some examples that highlight the syntax, please see the `examples/` directory.

As Cosmo is an embeddable scripting language, it is designed to be extended by the host program (from here on referenced as 'host'.) Cosmo provides extensive C API for the host to set up the Cosmo VM, modify state, add custom Proto objects, define custom globals and more.

Cosmo is also free and open source software, licensed under the MIT license (which can be found in `LICENSE.md`). For a reference on how code contributions can be made, please see `CONTRIB.md`.