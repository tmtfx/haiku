sed 's/^/:!/' lista_da_ignorare.txt | tr '\n' '\0' | xargs -0 git diff --name-only master -- .
