COMMENT  -*-pspp-*- .

COMMENT This program is useful for testing the behaviour of the Data and Variable Sheets.

input program.
vector var(500 F8.3).
  loop #c = 1 to 1000.
    loop #v = 1 to 500.
      compute var(#v) = #v + #c / 1000.
    end loop.
    end case.
  end loop.
  end file.
end input program.

variable label  var1 'First variable' var2 'Second variable' var3 'Third variable'.

save outfile='grid.sav'.

execute.
