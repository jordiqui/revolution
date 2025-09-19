@echo off

REM set openings to use
set opening=UHO_2024_8mvs_big_+100_+119

REM set the window title
title Season ultrabullet 2025 

REM cutechess-cli setup
@cutechess-cli.exe ^
  -event "SPRT TEST" -site "HP Proliant DL360P Gen8 Server" ^
  -engine conf="revolution-2.45-dev-180925" ^
  -engine conf="revolution-dev_v2.40_130925" ^
  -tournament gauntlet ^
  -each tc=10+0.1 option.Hash=32 option.Threads=1 -tb C:\Syzygy -tbpieces 5 ^
  -openings file=..\Openings\PGN\%opening%.pgn format=pgn order=random ^
  -concurrency 2 -rounds 1000 -games 2 ^
  -repeat -recover ^
  -pgnout ..\Games\games.pgn ^
  -ratinginterval 10
pause