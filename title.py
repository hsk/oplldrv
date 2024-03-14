import pygame, sys
screen = pygame.display.set_mode((600, 400))
pygame.font.init()
font = pygame.font.Font(None, 50)    
pygame.draw.rect(screen,(0,0,0),(0,0,600,400))
image2 = font.render(sys.argv[1], True, pygame.Color("white"))
screen.blit(image2,((600-image2.get_size()[0])/2,(400-image2.get_size()[1])/2))
pygame.image.save(screen,"title.png")
