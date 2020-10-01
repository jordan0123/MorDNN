# MorDNN

A neural network trained to play Mordhau. The neural network is an LSTM that learns to play like a human through supervised learning.
Data is collected through a process interface designed in C++ which is used to train a neural network in Python using Keras.

The neural network is given the game state which consists of the bone positions of the enemy (e.g. head, foot, hand) and is trained on output provided by a human. All behaviors exhibited by the program are learned and there are no pre-defined actions.

The process interface program utilizes Windows functions to read the process memory to extract the necessary data for the neural network. The interface utilizes an SDK generated by Unreal Finder Tool which extracts and provides a header-only library for reading from and writing to the necessary data structures of the game.
