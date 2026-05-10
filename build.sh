gcc -O3 -march=native -ffast-math -flto tools/nmustool/*.c -lm -o build/nmustool
gcc -O3 -march=native -ffast-math -flto=auto tools/npmitool/*.c tools/npmitool/vendor/*.c -lm -o build/npmitool
