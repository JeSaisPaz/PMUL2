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
  res.render('dashboard', { title: 'Dashboard' });
});

router.get('/orders', function(req, res, next) {
    res.render('orders', { title: 'Orders' });
});

router.get('/orders/:id', function(req, res) {
    res.render('orderDetails', { 
        title: "Commande #" + req.params.id,
        id: req.params.id 
    });
});

router.get('/neworder', async function(req, res, next) {
  try{
    const warehouse = await prisma.cOLOR.findMany({
      include: {
        ITEM: {
          where: {status: 'AVAILABLE'}
        }
      }
    });
    res.render('neworder', { 
      title: 'Place an order',
      warehouse
    });

  } catch(error){
    console.error("DB error :", error.message);
    res.render('neworder', { 
      title: 'Place an order',
      error: error.message,
      warehouse: []
    });
  }
});

router.get('/items', async function (req, res) {
  try{
    const items = await prisma.iTEM.findMany({
      include: {
        READ_CYCLES: true,
        SELECTION_HISTORY: true,
        COLOR: true,
      },
    });
    res.render('items', { 
      title: 'Items',
      items
    });

    router.get('/items/:id', function(req, res) {
      res.render('itemTracking', { 
        title: "Item #" + req.params.id,
        id: req.params.id 
      });
    });

  } catch(error){
    console.error("DB error :", error.message);
    res.render('items', { 
      title: 'Items',
      error: error.message, 
      items: []
    });
  }
});

module.exports = router;
