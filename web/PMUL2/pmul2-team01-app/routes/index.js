var express = require('express');
var router = express.Router();


const { PrismaClient } = require("../generated/prisma");

require("dotenv").config();
const { PrismaMariaDb } = require ("@prisma/adapter-mariadb");
//Temporaire
const adapter = new PrismaMariaDb({
  host: "192.168.1.167",
  user: "team01",
  password: "team01-thebestone",
  database: "team01-database",
  //pas oublier de changer le port plustard 3307-> 3306
  port: 3307,
  connectionLimit: 5,
});

const prisma = new PrismaClient({ adapter });

router.get('/', function(req, res, next) {
  res.render('homepage', { title: 'Homepage' });
});

router.get('/orders', function(req, res, next) {
  res.render('orders', { title: 'Orders' });
});

router.get('/items', async (req, res) => {
    const items = await prisma.ITEM.findMany({
    include: {
      READ_CYCLES: true,
      SELECTION_HISTORY: true,
    },
  });

    res.render('items', { title: 'Items', items});
});

module.exports = router;
