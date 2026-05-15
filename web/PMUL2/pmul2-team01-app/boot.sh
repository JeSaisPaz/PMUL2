#!/bin/sh
# Initialisation de la base de donnees et demarrage du serveur
npm i
npx prisma generate
npx prisma db push

echo "Demarrage du serveur..."
npm run dev