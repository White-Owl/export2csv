# export2csv
 Copyright 2016-2018, George Brink <siberianowl@gmail.com>


<p>Connects to database through specified DSN.
<p>Extracts specified table, view, or query into a text file.
<p>Can specify delimiters and (if necessary) qoute the individual fields.

<p>Multiple resultsets can be handled. The first one is stored in the
specified file, the second and all other are stored in files with names
like "name-XX.ext" where 'name' and 'ext' are taken from the original
output file.

<p>Examples of usage:
 
- Assuming you have DSN (named MyDB) with saved user id and password (or
database server does not require them), you can do:<br>
   export2csv "select * from Customers" customers.csv -DMyDB
   
- Or in a shortened form:<br>
   export2csv Customers customers.csv -DMyDB<br>
 In this case, the tool recognize that Customers is a name of the table and
prepend it with "select * from" automatically.

- Another example, is if you have a script in a file with multiple select
statements, like<br>
 &nbsp;&nbsp;&nbsp;&nbsp;------ get_selected_customers.sql<br>
 &nbsp;&nbsp;&nbsp;&nbsp; select * from Customers where City='New York';<br>
 &nbsp;&nbsp;&nbsp;&nbsp; select * from Customers where City='Boston';<br>
 &nbsp;&nbsp;&nbsp;&nbsp; select * from Customers where City='Chicago';<br>
 Now run this script through export2csv<br>
&nbsp;&nbsp;&nbsp;&nbsp;   export2csv get_selected_customers.sql customers.csv -DMyDB<br>
 And you will receive three files:<br>
&nbsp;&nbsp;&nbsp;&nbsp;  customers.csv    - data from the first select (New York)<br>
&nbsp;&nbsp;&nbsp;&nbsp;  customers_01.csv - second select (Boston)<br>
&nbsp;&nbsp;&nbsp;&nbsp;  customers_02.csv - third select (Chicago)<br>




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
