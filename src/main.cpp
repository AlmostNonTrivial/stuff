#include "executor.hpp"


const char * create_customers = "CREATE TABLE Customers (id TYPE_4, name TYPE_32, email TYPE_32)";
const char * insert_customer = "INSERT INTO Customers VALUES (1, 'john', 'john@smith.com')";
const char * select_customers = "SELECT * FROM Customers";



int main() {


    execute(create_customers);
    execute(insert_customer);
    execute(select_customers);


  return 0;

}
