CREATE TABLE Data
(
  `Key` CHAR(8) NOT NULL,
  `Value` VARCHAR(32) NOT NULL,
  PRIMARY KEY(`Key`)
);

CREATE TABLE Gateways
(
  `Call` CHAR(8) NOT NULL,
  `Address` VARCHAR(64) NOT NULL,
  PRIMARY KEY(`Call`)
);

CREATE TABLE Remotes
(
  `Call` CHAR(8) NOT NULL,
  `Address` VARCHAR(64) NOT NULL,
  `NAT` INTEGER DEFAULT 0,
  PRIMARY KEY(`Call`)

);

CREATE TABLE Spots
(
  `Call` CHAR(8) NOT NULL,
  `Address` VARCHAR(64) NOT NULL,
  PRIMARY KEY(`Call`)
);

CREATE TABLE Tables
(
  `Table` INTEGER NOT NULL,
  `Call1` CHAR(8) NOT NULL,
  `Call2` CHAR(8) NOT NULL,
  `Date` DATETIME NOT NULL,
  PRIMARY KEY(`Table`, `Call1`)
);

CREATE TABLE Users
(
  `Nick` VARCHAR(16) PRIMARY KEY,
  `Name` VARCHAR(16),
  `Address` VARCHAR(64),
  `Enabled` INTEGER DEFAULT 1,
  `Date` TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX TableDate ON Tables (`Table`, `Date`);

delimiter //

CREATE TRIGGER RegisterCall AFTER INSERT ON Tables FOR EACH ROW
BEGIN
  IF (NEW.`Table` = 0) AND (NEW.Call1 NOT IN (SELECT `Key` FROM Data)) THEN
    INSERT
      INTO Data (`Key`, `Value`)
      VALUES (NEW.`Call1`, '');
  END IF;
END;
//

CREATE TRIGGER RegisterUser AFTER INSERT ON Users FOR EACH ROW
BEGIN
  IF (NEW.`Enabled` = 1) AND (NEW.`Nick` LIKE '%-_') THEN
    REPLACE
      INTO Gateways (`Call`, `Address`)
      VALUES (SUBSTR(CONCAT(UPPER(NEW.`Name`), '        '), 1, 8), NEW.`Address`);
  END IF;
END;
//

CREATE TRIGGER RegisterGateway AFTER INSERT ON Gateways FOR EACH ROW
BEGIN
  REPLACE
    INTO Data (`Key`, `Value`)
    VALUES (NEW.Call, NEW.Address);
  REPLACE
    INTO Remotes (`Call`, `Address`)
    VALUES (NEW.Call, NEW.Address);
END;
//

CREATE TRIGGER RegisterSpot AFTER INSERT ON Spots FOR EACH ROW
BEGIN
  REPLACE
    INTO Data (`Key`, `Value`)
    VALUES (NEW.Call, NEW.Address);
  REPLACE
    INTO Remotes (`Call`, `Address`, `NAT`)
    VALUES (SUBSTR(CONCAT(NEW.Call, '        '), 1, 8), NEW.Address, 1);
END;
//

delimiter ;
