#!/bin/sh

TRANSLATE ()
{
    echo "$1";
}

extract_string ()
{
  msggrep -K -F -e "$2" $top_builddir/$1 2> /dev/null | msgattrib --translated --no-fuzzy | awk -F \" '/^msgstr "/{print $2}' | grep -v '^$' 
}


GenericName=`TRANSLATE "Statistical Software"`
Comment=`TRANSLATE "Analyze statistical data with a free alternative to SPSS"`


printf "# Generated from $0        -*- buffer-read-only: t -*-\n\n"

printf '[Desktop Entry]\n';
printf 'Name=GNU PSPP\n';
printf "GenericName=$GenericName\n"

for pfile in $POFILES; do 
    lang=${pfile%%.po}
    lang=${lang##po/}
    xlate=`extract_string $pfile "$GenericName"`
    if test -z "$xlate"; then continue; fi
    printf "GenericName[$lang]=$xlate\n";
done


printf "Comment=$Comment\n"

for pfile in $POFILES; do 
    lang=${pfile%%.po}
    lang=${lang##po/}
    xlate=`extract_string $pfile "$Comment"`
    if test -z "$xlate"; then continue; fi
    printf "Comment[$lang]= $xlate\n";
done

printf 'Exec=psppire %%F\n'
printf 'Icon=pspp%s\n'
printf 'Terminal=false%s\n'
printf 'Type=Application%s\n'
printf 'Categories=GTK;Education;Science;Math;%s\n'
printf 'MimeType=application/x-spss-sav;application/x-spss-por;%s\n'
printf 'StartupNotify=false%s\n'

