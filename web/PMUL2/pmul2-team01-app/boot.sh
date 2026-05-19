#!/bin/sh
# Initialisation de la base de donnees et demarrage du serveur
npm i
npx prisma migrate deploy
npx prisma generate

echo "Demarrage du serveur..."
npm run dev