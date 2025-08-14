from sqlalchemy import Column, Integer, String, ForeignKey
from sqlalchemy.orm import relationship
from database import Base

class User(Base):
    __tablename__ = 'users'

    id = Column(Integer, primary_key=True, index=True)
    email = Column(String, unique=True, index=True)
    hashed_password = Column(String)
    licenses = relationship("License", back_populates="owner")
    devices = relationship("Device", back_populates="user")

class License(Base):
    __tablename__ = 'licenses'

    id = Column(Integer, primary_key=True, index=True)
    user_id = Column(Integer, ForeignKey('users.id'))
    product_key = Column(String, unique=True)
    is_active = Column(Integer)  # 1 for active, 0 for inactive
    owner = relationship("User", back_populates="licenses")

class Device(Base):
    __tablename__ = 'devices'

    id = Column(Integer, primary_key=True, index=True)
    user_id = Column(Integer, ForeignKey('users.id'))
    hwid = Column(String, unique=True)
    user = relationship("User", back_populates="devices")

class RecToken(Base):
    __tablename__ = 'rec_tokens'

    id = Column(Integer, primary_key=True, index=True)
    user_id = Column(Integer, ForeignKey('users.id'))
    token = Column(String, unique=True)
    is_valid = Column(Integer)  # 1 for valid, 0 for invalid
    user = relationship("User")