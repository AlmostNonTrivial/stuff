#include "executor.hpp"


const char * create_customers = "CREATE TABLE Customers (INT id, VAR32 name, VAR32 email);";
const char * insert_customer = "INSERT INTO Customers VALUES (1, 'john', 'john@smith.com');";
const char * select_customers = "SELECT * FROM Customers;";
const char * select_tables= "SELECT * FROM sqlite_master;";



int main() {

    _debug = false;
    execute(create_customers);
    execute(insert_customer);
    execute(select_customers);
    execute(select_tables);


  return 0;

}
