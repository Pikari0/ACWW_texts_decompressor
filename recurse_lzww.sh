#/bin/bash
shopt -s globstar
for f in $1/**
do
  if [ ! -d $f ]; then
    ./lzww -d $f
  fi
done
