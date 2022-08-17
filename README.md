TFT_CompBuilder
# Teamfight Tactics Team Composition Generator
## Game Background
  Teamfight Tactics is a game made by Riot Games. This game is an 'auto-battler' where you build teams of characters and battle other players' teams over several rounds. Characters are called "champions" and each champion has a few attributes called traits. Many champions share traits, so when you build a team, the game keeps track of each trait's total across all champions in the team. Each trait also has milestones which gives teams a bonus. The milestones are reached by having enough of the trait in your team. For example, the "Guardian" trait has a milestone for when 2, 4, or 6 champions have the trait and provides an increasingly powerful defensive bonus to your team the higher the milestone your has reached. These differing levels of each trait milestone are what I call trait tiers. When just the first milestone of a trait is reached, the trait is considered active. One of the main points of engagement for Teamfight Tactics is the strategy and planning that goes into making a team with many active and useful traits.
  
## Purpose
  This program is meant to find the possible team compositions in the game that have the most active traits. There are two main ways to measure traits: by counting the traits that are simply active, or by counting the levels/tiers of each active trait. This program can be configured to find comps using either method. The number of all the possible combinations of champions for a team composition of size n is given by the equation $\frac{7\cdot47!}{\left(49-n\right)!\left(n-2\right)!}+\frac{51!}{\left(51-n\right)!n!}$ (This equation does not consider team compositions that have traits that cancel out, such as having both "Dragon" and "Scalescorn").

## Future


## Terminology
  Teamfight Tactics (TFT) - The name of the game where you build team compositions with champions and their traits.
  Set - The current rotation of champions and traits available in Teamfight Tactics. This comp-builder program is based on TFT's 7th Set. To not be confused with the data structure, this Set is referred to with capital S in code comments.
  Team Composition (comp) - A set of champions. Typically a comp will not have more than 9 slots for champions, and this program is based around that. 
  Champion (champ) - A character in the game that has traits and takes up space in a comp, typically only 1 slot but sometimes 2.
