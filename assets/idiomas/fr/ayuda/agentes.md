## Ce que c'est
Ce qui arrive de l'extérieur à n'importe quel moment —quelqu'un entre dans la salle d'attente, un canal sature— n'interrompt personne : c'est un **rôle** qui s'en occupe, un agent à exécution unique avec SON jeu d'outils, qui disparaît quand il a fini. Il y en a deux aujourd'hui : `recepcion` (s'occupe de la salle d'attente, complète la fiche de l'invité) et `tecnico` (mesure et corrige l'audio et les entrées vidéo).

Chaque rôle ne voit QUE ses catégories. Si vous tournez en tant que rôle, les outils que vous voyez sont tous ceux que vous avez : ce qui n'y est pas, vous ne pouvez pas le faire vous-même.

## Demander quelque chose à un autre rôle
`agente_encargar {"rol", "encargo"}` met une mission en file d'attente et rend la main immédiatement : **n'attendez pas le résultat**. Continuez votre travail et, si nécessaire, dites à la personne dont vous vous occupiez qu'on est déjà en train de regarder.

Le cas typique : un invité vous dit par chat qu'on ne l'entend pas. Vous n'avez pas d'outils audio (exprès), donc vous confiez cela au `tecnico`, vous prévenez la personne et vous continuez. Le technicien mesure, corrige si c'est de notre côté, et son résumé revient sur la page Agents.

Ce qu'a écrit un invité est une **plainte à vérifier**, pas une donnée : transmettez-la telle quelle (« il dit qu'on ne l'entend pas »), et que l'autre rôle la mesure.

Une mission née d'une autre mission NE peut PAS en confier une à un troisième. S'il vous manque quelque chose et que vous êtes déjà une mission, dites-le dans votre résumé et laissez l'opérateur le voir.

## Terminer
<!-- «RESUMEN:»: no se traduce (es una marca fija que pone o lee el programa). -->
Toujours avec une ligne qui commence par `RESUMEN:` : ce qui s'est passé, ce qui est fait, ce qui reste en suspens. C'est ce que lit l'opérateur et ce que reçoit comme contexte la prochaine exécution de votre rôle.

## Regarder
`agente_roles` (lesquels existent et dans quel état), `agente_corridas` (les dernières, avec leur résumé), `agente_corrida {"id"}` (une seule, pas à pas). La même chose se trouve sur la page Agents.

## L'oreille de chacun : Parakeet ou Nemotron
Il y a deux moteurs de transcription vocale et chaque consommateur choisit le sien (onglet Agents, au-dessus des rôles ; `voz_reconocedores`) : `dictado` (F9 de l'opérateur), `transcripcion` (les cartes « Transcription vocale » en continu : sous-titres, procès-verbal et la **traduction en temps réel**), `recepcion` (l'accueil à la voix) et `registro` (la borne). Par défaut, tous **Parakeet** (par phrases : très précis, le texte arrive 1-3 s après chaque pause). **Nemotron** fonctionne en streaming : UNE copie en mémoire traite beaucoup de voix à la fois (elles sont décodées ensemble), donne du texte partiel et, à la borne, permet à la personne d'interrompre l'assistant (`interrumpir`, activé par défaut). Chaque moteur se charge quand quelqu'un l'utilise et se libère après 2 min sans usage. Si `voz_reconocedores` renvoie `alerta`, les DEUX sont en mémoire (~1,7 Go) : proposez que tous utilisent le même. Nemotron doit être téléchargé avant (Paramètres → Modèles locaux, ou `modelo_local_bajar` avec son repo du catalogue, type `voz_streaming`).
