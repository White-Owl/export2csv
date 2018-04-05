# export2csv
 Copyright 2016-2018, George Brink <siberianowl@gmail.com>


 Connects to database through specified DSN.
 Extracts specified table, view, or query into a text file.
 Can specify delimiters and (if necessary) qoute the individual fields.

 Multiple resultsets can be handled. The first one is stored in the
specified file, the second and all other are stored in files with names
like "name-XX.ext" where 'name' and 'ext' are taken from the original
output file.

Examples of usage:
 Assuming you have DSN (named MyDB) with saved user id and password (or
database server does not require them), you can do:
   export2csv "select * from Customers" customers.csv -DMyDB
 Or in a shortened form:
   export2csv Customers customers.csv -DMyDB
 In this case, the tool recognize that Customers is a name of the table and
prepend it with "select * from" automatically.

 Another example, is if you have a script in a file with multiple select
statements, like
  ------ get_selected_customers.sql
  select * from Customers where City='New York';
  select * from Customers where City='Boston';
  select * from Customers where City='Chicago';
 Now run this script through export2csv
   export2csv get_selected_customers.sql customers.csv -DMyDB
 And you will receive three files:
  customers.csv    - data from the first select (New York)
  customers_01.csv - second select (Boston)
  customers_02.csv - third select (Chicago)




-- Note for Windows:
 Works perfectly with MinGW or its clones. Just run make.
 Should work with other compilers, but not tested.

-- Note for Linux:
 Written and tested with unixODBC in mind. Possibly will work with other ODBC
libraries. If someone willing to test - please send me a letter.
 The makefile uses odbc_config tool, which comes standard on Red Hat, but by
some reason is not available from the box in Debian offsprings (Ubuntu, Mint).
But it is possible to find the script source on the web.


--- TODO
- Autorecognition of objects (if you pass it just the name of the table) is
written with Tranasct-SQL code (think Sybase ASE and SQL Server). That piece
needs to be rewritten to a more universal way.
