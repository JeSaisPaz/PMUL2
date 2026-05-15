const { PrismaClient, ORDER_STATUS, ITEM_STATUS, DECISION, TEAM, DECISION_STATUS } = require("../generated/prisma");
const { PrismaMariaDb } = require("@prisma/adapter-mariadb");
require("dotenv").config();

const adapter = new PrismaMariaDb({
    host: "mysql",
    user: process.env.MYSQL_USER,
    password: process.env.MYSQL_PASSWORD,
    database: process.env.MYSQL_DATABASE,
    port: 3306,
    connectionLimit: 5,
});

const prisma = new PrismaClient({ adapter });

module.exports = { prisma, ORDER_STATUS, ITEM_STATUS, DECISION, TEAM, DECISION_STATUS };
